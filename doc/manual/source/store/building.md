# Building

As discussed in the [main page on derivations](./derivation/index.md):

> A derivation is a specification for running an executable on precisely defined input to produce one or more [store objects][store object].

This page describes *building* a derivation, which is to say following the instructions in the derivation to actually run the executable.
Some elements of derivations are self-explanatory.
For example, the arguments specified in the derivation really are the arguments passed to the executable.
In other cases, however, there is additional common steps performed by Nix for all derivations --- mostly for setting up the build environment and collecting the built outputs.

The chief design consideration for the building process is *determinism*.
Conventional operating systems are typically not designed with determinism in mind.
But determinism is needed to make Nix's build caching a transparent abstraction.

> **Explanation**
>
> For example, no one wants to slightly modify a derivation, and then find that it no longer builds for an unrelated reason, because the original derivation *also* doesn't build anymore, but the cache hit on the original derivation was hiding this.
> We want builds that succeed once to continue succeeding, to encourage fearless modification of old build recipes.
> Determinism is what enables things that once worked to keep working.

The life cycle of a build can be broken down into 3 parts:

1. Spawn the builder process with the proper environment, including the correct process arguments, environment variables, and file system state.

2. Wait for the builder process to exit and collect its exit status.
   Exit code 0 means success; anything else is a build failure.
   (Strictly speaking, Nix detects process exit by waiting for the standard output and error streams to close.
   If a builder explicitly closes these streams without exiting, Nix will kill it, and deem the build a failure.
   Processes should therefore exit *without* explicitly closing those standard streams, and let the exiting of the process close them implicitly.)

   Nix also logs the standard output and error of the process, but this is just for human convenience and does not influence the behavior of the system.
   (Builder processes have no idea what the consumer of their standard output and error does with the pseudo-terminal master, only that they are indeed consumed so buffers do not fill up etc. and writes to each output standard stream will continue to succeed.
   In practice, Nix will store the log in `/nix/var/log/nix`)

3. Processing the outputs.

   Traditionally, this happens only after the builder has exited: the builder process should have left behind files for each output the derivation is supposed to produce, and those files are processed to turn them into bona fide store objects.
   But there is now also a second approach where the builder sends messages to Nix while it's running, including messages submitting outputs.
   This allows outputs to be processed concurrently during the build, allows outputs to depend on other newly created store objects, and also resolves some tricky issues with content-addressing and output-to-output references.
   If the processing succeeds, the resulting store objects are associated with the derivation as (the results of) a successful build.

Step (3) is done by Nix, either externally to the build (in the traditional case, operating on the inert data left behind after the builder has exited or been killed) or concurrently with it (in the IPC case).
Step (1) however is best described not from Nix's perspective, but from the build process's perspective.

> **Explanation**
>
> Ultimately, what matters for determinism is what the build process can observe: what resources (files, networking, etc.) it can see, what syscalls succeed or fail, etc.
> Nix can achieve this through many different sandboxing strategies (namespaces, VMs, chroots, ...), but the process shouldn't be able to tell them apart.
> We therefore specify building from the process's perspective, not Nix's perspective, to focus on *what*, not *how*.

## What derivations can be built

Actually only some derivations are ready to be built.
In particular, only [*resolved*](./resolution.md) derivations can be built.
That is to say, a derivation that depends on other derivations is not ready yet to be built, because some of those other derivations might not have yet been built.
If the other derivations are indeed all built, we can witness this fact by resolving the derivation, and converting all the derivation's input references into plain store paths.

> **Note**
>
> Note that [input-addressing](derivation/outputs/input-address.md) derivations are improperly resolved.
> As discussed on the linked page, the current input-addressing algorithm does not respect resolution-equivalence of derivations (\\(\\sim_\mathrm{Drv}\\)).
> That means that if Nix properly resolved an input-addressed derivation, the resolved derivation would have different input addresses, violating expectations.
> Nix therefore improperly resolves the derivation, keeping its original input-addressed output paths, creating an invalid derivation that is both resolved and instructed to create the outputs at the originally expected paths.

## Environment of the builder process

This section describes how the [`builder`](./derivation/index.md#builder) is executed.

> **Implementation detail**
>
> Nix prevents multiple [Nix instances][Nix instance] from performing the same build at the same time, for example by acquiring exclusive file locks.

### File system

The builder should have access to a limited file system where only certain objects are available.
The most important exposed files are the inputs (other store objects) of the (resolved) derivation.
Additionally, some other files are exposed.

#### Store inputs

The builder will be run against a file system in which the [store directory][store directory path] contains the [closure] of the inputs.
In particular, consider a store that just contains this closure.
That store is exposed to the file system according to the rules specified in the [Exposing Store Objects in OS File Systems](./store-path.md#exposing) documentation.
This precisely defines the file system layout of the store that should be visible to the builder process.

> **Note**
>
> Historically, Nix exposed *at least* the following store contents to the builder, but also arbitrarily other store objects, due to limitations around operating systems' file system virtualization capabilities, and wanting to avoid copying or moving files.
> It still can do this in so-called *unsandboxed* builds.
>
> Such builds should be considered discouraged, but one that works less badly against non-mischievous derivations than might be expected.
> This is because store paths are relatively unpredictable, so a well-behaved program is unlikely to stumble upon a store object it wasn't supposed to know about.
>
> As operating systems developed better file system primitives, the need for disabling sandboxing has lessened greatly over the years, and this trend should continue into the future.

The outputs are expected to be created in that store directory as if they were valid store objects.
(They are just files during builder execution, but during [processing outputs](#processing-outputs) they will be turned into proper store objects.)
The [environment variables](#env-vars) for each output indicate where the builder should write them;
Nix ensures that those paths do not yet exist when the builder is run.

> **Note**
>
> In sandboxed builds, ensuring that the outputs do not exist in the store directory is trivial.
> In unsandboxed builds, it is harder in general.
> In the worst case, the derivation is in fact rewritten so different output paths are used instead, and then the outputs are rewritten back to the intended output paths after.
> In the content-addressing case rewriting would be needed either way, but in the input-addressing case, this is a significant degradation, as the point of input addressing is to avoid rewrites by knowing output paths in advance.

[realised]: @docroot@/glossary.md#gloss-realise
[closure]: @docroot@/glossary.md#gloss-closure
[store directory path]: ./store-path.md#store-directory-path

### Other file system state

- The current working directory of the builder process will be a fresh temporary directory.
  It is initially empty when the process starts except for a few input files:

  - If [`__structuredAttrs`](@docroot@/language/advanced-attributes.md#adv-attr-structuredAttrs) is enabled: `.attrs.json` (the derivation attributes as JSON) and `.attrs.sh` (a Bash-compatible rendering of the same).
    The environment variables `NIX_ATTRS_JSON_FILE` and `NIX_ATTRS_SH_FILE` point to these files, respectively.

  - If [`passAsFile`](@docroot@/language/advanced-attributes.md#adv-attr-passAsFile) is used (only without `__structuredAttrs`): for each attribute name listed, a file `.attr-<hash>` where `<hash>` is the [Nix32](@docroot@/protocols/nix32.md)-encoded SHA-256 hash of the attribute name.
    The environment variable `<name>Path` points to the file containing the attribute's value.

  In sandboxed builds, this directory is at a deterministic path inside the sandbox (controlled by the [`sandbox-build-dir`](@docroot@/command-ref/conf-file.md#conf-sandbox-build-dir) setting, default `/build`).
  See also the per-store [`build-dir`](@docroot@/store/types/local-store.md#store-local-store-build-dir) setting for the host-side location.

- Basic device nodes for essential operations (null device, random number generation, standard streams as a pseudo terminal)

  (A pseudo terminal would not be strictly necessary since the standard streams are passively logging, not there to facilitate interaction.
  But it is still useful to entice programs to do nicer logging with e.g. colors etc.)

- On Linux: Process information via `/proc`

- Minimal user and group identity information

- A loopback-only network configuration with hostname set to `localhost`

> **Note**
>
> Fixed-output derivations have access to additional operating system state to facilitate communication with the outside world, such as network name resolution and TLS certificate verification.
> This is necessary because these derivations are allowed to access the network, unlike regular derivations which are fully sandboxed.

### Environment variables {#env-vars}

The environment is cleared and set to the derivation attributes, as
specified above.

For most derivations types this must contain at least:

- For each output declared in `outputs`, the corresponding environment variable is set to point to the intended path in the Nix store for that output.
  Each output path is a concatenation of the cryptographic hash of all build inputs, the `name` attribute and the output name.
  (The output name is omitted if it's `out`.)

In addition, the following variables are set:

- `NIX_BUILD_TOP` contains the path of the temporary directory for this build.

- Also, `TMPDIR`, `TEMPDIR`, `TMP`, `TEMP` are set to point to the temporary directory.
  This is to prevent the builder from accidentally writing temporary files anywhere else.
  Doing so might cause interference by other processes.

- `PATH` is set to `/path-not-set` to prevent shells from initialising it to their built-in default value.

- `HOME` is set to `/homeless-shelter`.
   (Without sandboxing, this discourages programs from using `/etc/passwd` or the like to find the user's home directory, which could cause impurity.)
   Usually, when `HOME` is set, it is used as the location of the home directory, even if it points to a non-existent path.

- `NIX_STORE` is set to the path of the top-level Nix [store directory path] (typically, `/nix/store`).

- `NIX_ATTRS_JSON_FILE` & `NIX_ATTRS_SH_FILE` if `__structuredAttrs` is set to `true` for the derivation.
  A detailed explanation of this behavior can be found in the [section about structured attrs](@docroot@/language/advanced-attributes.md#adv-attr-structuredAttrs).

### Arguments

The builder is passed the arguments specified by the derivation attribute `args`.

## Processing outputs

There are two methods for processing outputs.
But first, let us cover the requirements common to both methods.

Regardless of which method is used, each output must be turned into a valid store object.
This involves two steps:

- **Normalize the file permissions**

  The files must conform to the model described in the [Exposing in OS file systems](./file-system-object/os-file-system.md) section.
  For example, timestamps and permissions are canonicalised.

- **Calculate the references**

  Nix scans each output path for [references] to input store objects by looking for the [digest][store path digest] of each input.
  (The name part and the [store directory path] are ignored when scanning; an input's hash part that is neither followed by a `-` nor proceeded by a `/` still scans as a reference.)
  Since these are potential runtime dependencies, Nix will register them as references of the output store object they occur in.

### Traditional (post-build) processing

With the traditional method, the builder process on exit should have left behind files for each output the derivation is supposed to produce.
The files must be processed to turn them into bona fide store objects.
If the processing succeeds, those store objects are associated with the derivation as (the results of) a successful build.

Nix also scans for references from one output to another in the same way, because outputs are allowed to refer to each other.
The outputs' references must form a [directed acyclic graph](@docroot@/glossary.md#gloss-directed-acyclic-graph).
(This is not a special restriction for outputs; it is true for the references of all store objects in general.)

In the case of derivations with output paths that are fixed in advance (i.e. [input-addressing] derivations, or [fixed content-addressing] derivations), the actual final store path to each output is used during the build if possible.
For [floating content-addressing] derivations, however, the final store path is not known in advance by definition.
Scratch store paths must therefore be used instead.
Scanning will use those scratch paths, but then any output-to-be that contains such a scanned scratch path must be rewritten to instead use the final (content-addressed) path of the output in question.

In addition to output-to-output references, rewriting is also needed to support self-references in the content-addressing case.
An output may contain its own store path digest, which is a self-reference.
Hash functions which are secure cannot allow the easy calculation of the quasi-fixed points needed to support self-references "natively", so instead we replace all would-be self-references with a sentinel value, and then rewrite the sentinel value to be the final store path digest.
Superficially, this post-hashing rewriting breaks the content address, but as the self-references are easily identified, the rewriting can be inverted to yield the original hashed data, allowing verifying the content address after all.

At this point, the file system data is in the proper form, and the valid acyclic reference data for each output is also calculated, so the outputs are added to the store as proper store objects.
Additionally, those store objects (at least in the case that they are [content-addressed][content-addressing]) can be associated with the derivation in the [build trace] in the record for a successful build.

> **Implementation detail**
>
> Nix will normally clean up and remove the temporary build directory after every build, successful or unsuccessful.
> The builder doesn't know whether Nix does or not, however, as it will have exited before the build directory is cleaned up, and it will not see any old build directory if (after a failed build) it is run again.
> The [`--keep-failed`](@docroot@/command-ref/opt-common.md#opt-keep-failed) option can be specified to keep the build directory in the case of a failing build.

### Concurrent processing via IPC

With this method, the builder communicates with Nix during the build using inter-process communication (IPC).
(The exact varlink-based protocol used is [documented in full in the protocols chapter](@docroot@/protocols/derivation-builder/index.md).)
Instead of leaving files behind for Nix to process after exit, the builder explicitly submits information to create store objects one at a time, and (separately) also submits assignments from output names to store objects.

Scanning for references proceeds as usual for each store object creation request, but the set of potential references to be scanned is greater: it includes both all inputs (as before) and also all previously-added store objects.
This means, if output `bar` is supposed to reference output `foo`, `foo` should be created first, and `bar` second.

All store objects being created are content-addressed (there is no support for input-addressed outputs with the IPC approach).
When a store object is created, its content address store path will be calculated by Nix and then returned in the IPC response message.
The builder then knows what store path to use in subsequent store objects in order for reference scanning to pick them up.

This overall approach has several advantages:

- **No Nix-side rewriting**

  For content-addressed outputs, the builder is responsible for adding outputs in reference order, using the store paths from earlier adds in later ones.
  This avoids the fragile rewriting that would otherwise be needed to fix up output-to-output references described above.
  The builder, unlike Nix itself, is free to leverage domain-specific knowledge to do a better job. For example it can

  - uncompress, rewrite, and then recompress man pages, to not miss references hidden by compression.

  - make sure to rewrite data that is to be signed, like Apple binaries, before signing that data, so as not to invalidate any signatures by mistake.

- **Pipelining**

  Downstream builds that only need some outputs (e.g., a "dev" or "headers" output) can start without waiting for all outputs to be ready.
  Nix doesn't yet implement this, but it could and should.

The major *disadvantage* of this approach is that it doesn't yet support self-references.
Unlike acyclic output-to-output references, self-references fundamentally do require rewriting.
The output-to-output case was only a challenge in the traditional case because all the outputs were submitted simultaneously, whereas the self-reference case is fundamentally challenging because of what it means for a hash function to be secure, as described above.
Neither batched (traditional) nor serial (IPC) submission of outputs can avoid this fundamental property of secure hash functions.
We could add support for such rewriting just for self-references, as is done for the traditional post-build processing, but we haven't yet done so as the very point of the IPC approach is to free Nix from any obligation to rewrite black-box data in unsound ways.

[references]: ./store-object.md#references
[store path digest]: ./store-path.md#digest
[store object]: ./store-object.md
[Nix instance]: @docroot@/glossary.md#gloss-nix-instance
[content-addressing]: ./derivation/outputs/content-address.md
[input-addressing]: ./derivation/outputs/input-address.md
[fixed content-addressing]: ./derivation/outputs/content-address.md#fixed
[floating content-addressing]: ./derivation/outputs/content-address.md#floating
[build trace]: ./build-trace.md
