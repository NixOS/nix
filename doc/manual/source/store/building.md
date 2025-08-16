# Building

As discussed in the [main page on derivations](./derivation/index.md):

> A derivation is a specification for running an executable on precisely defined input to produce one or more [store objects][store object].

This page describes *building* a derivation, which is to say following the instructions in the derivation to actually run the executable.
In some cases the derivation is self-explanatory.
For example, the arguments specified in the derivation really are the arguments passed to the executable.
In other cases, however, there is additional procedure true for all derivations, which is therefore *not* specified in the derivation.
This page specifies this invariant procedure that is true for all derivations, too.

The chief design consideration for the building process is *determinism*.
Conventional operating systems are typically not designed with determinism in mind.
But determinism is needed to make Nix's caching a transparent abstraction.

> **Explanation**
>
> For example, no one wants to slightly modify a derivation, and then find that it no longer builds for an unrelated reason, because the original derivation *also* doesn't build anymore, but the cache hit on the original derivation was hiding this.
> We want builds that once succeed to continue succeeding, to encourage fearless modification of old build recipes.
> Determinism is what enables things that once worked to keep working.

The life cycle of a build can be broken down into 3 parts:

1. Spawn the builder process with the proper environment, including the correct process arguments, environment variables, and file system state.

2. Wait for the standard output and error of the process to be closed and/or the process to exit.
   (If the standard streams are closed but the process hasn't exited, Nix will kill the process.)

   Nix also logs the standard output and error of the process, but this is just for human convenience and does not influence the behavior of the system.
   (Builder processes have no idea what the consumer of their standard output and error does with those streams, only that they are indeed consumed so buffers do not fill up and writes to them will continue to succeed.)

3. Processing the outputs

   Traditionally, this happened only after the builder has exited.
   But there is now also a second approach where the builder sends messages to Nix while it's running, messages including submitting outputs.
   This allows outputs to be processed concurrently during the build, allows outputs to depend on other newly created store objects, and also resolves some tricky issues with content-addressing and output-to-output references.

Step (3) happens externally, with inert data (either by forcing the process to relinquish access to the data, by such means as making a fresh copy and/or killing the process, its process group, etc.).
Step (1) however is best described not from Nix's perspective, but from the process's perspective.

> **Explanation**
>
> Ultimately, what matters for determinism is the behavior of IO operations that the process attempts (whether these are successes or failures), because of how they affect the output files, and how they affect the further execution of the builder process.
> From Nix (and the operating system)'s perspective, there are many, many different ways --- different implementation strategies --- of effecting the same I/O behavior,
> But from the process's perspective, there is only one correct behavior.

## What derivations can be built

Actually only some derivations are ready to be built.
In particular, only [*resolved*](./resolution.md) derivations can be built.
That is to say, a derivation that depends on other derivations is not ready yet to be built, because those other derivations might not be built.
If the other derivations are indeed built, we can witness this fact by resolving the derivation, and converting all the derivation's input references into plain store paths.

> **Note**
>
> Note that [input-addressing](derivation/outputs/input-address.md) derivations are improperly resolved.
> As discussed on the linked page, the current input-addressing algorithm does not respect resolution-equivalence of derivations (\\(\\sim_\mathrm{Drv}\\)).
> That means that if Nix properly resolved an input-addressed derivation, the resolved derivation would have different input addresses, violating expectations.
> Nix therefore improperly resolves the derivation, keeping its original input address output paths, creating an invalid derivation that is both resolved and instructed to create the outputs at the originally expected paths.

## Environment of the builder process

The [`builder`](./derivation/index.md#builder) is executed as follows:

### File system

The builder should have access to a limited file system where only certain objects are available.
The most important exposed files are the inputs (other store objects) of the (resolved) derivation.
Additionally, some other files are exposed.

#### Store inputs

The builder will be run against a file system in which the [closure] of the inputs is mounted inside the [store directory][store directory path].
In particular, consider a store that just contains this closure.
That store may be exposed to the file system according to the rules specified in the [Exposing Store Objects in OS File Systems](./store-path.md#exposing) documentation.
This precisely defines the file system layout of the store that should be visible to the builder process.

> **Note**
>
> Historically, Nix exposed *at least* the following store contents to the builder, but also arbitrarily other store objects, due to limitations around operating systems' file system virtualization capabilities, and wanting to avoid copying or moving files.
> It still can do this in so-called *unsandboxed* builds.
>
> Such builds should be considered an unsafe extension, but one that works less badly against non-malicious derivations than might be expected.
> This is because store paths are relatively unpredictable, so a well-behaved program is unlikely to stumble upon a store object it wasn't supposed to know about.
>
> As operating systems developed better file system primitives, the need for disabling sandboxing has lessened greatly over the years, and this trend should continue into the future.

[realised]: @docroot@/glossary.md#gloss-realise
[closure]: @docroot@/glossary.md#gloss-closure
[store directory path]: ./store-path.md#store-directory-path

### Other file system state

- The current working directory of the builder process will be a fresh temporary directory that is initially empty.

  Nix uses a directory within `TMPDIR` (default `/tmp`) for this purpose.
  (This is the outside world's `TMPDIR`; this implementation detail is not a normative part of the specification for building.)

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
  (The output name is omitted if it’s `out`.)

In addition, the following variables are set:

- `NIX_BUILD_TOP` contains the path of the temporary directory for this build.

- Also, `TMPDIR`, `TEMPDIR`, `TMP`, `TEMP` are set to point to the temporary directory.
  This is to prevent the builder from accidentally writing temporary files anywhere else.
  Doing so might cause interference by other processes.

- `PATH` is set to `/path-not-set` to prevent shells from initialising it to their built-in default value.

- `HOME` is set to `/homeless-shelter`.
   (Without sandboxing, this serves as "soft sandboxing" --- it discourages programs from using `/etc/passwd` or the like to find the user's home directory, which could cause impurity.)
   Usually, when `HOME` is set, it is used as the location of the home directory, even if it points to a non-existent path.

- `NIX_STORE` is set to the path of the top-level Nix [store directory path] (typically, `/nix/store`).

- `NIX_ATTRS_JSON_FILE` & `NIX_ATTRS_SH_FILE` if `__structuredAttrs` is set to `true` for the derivation.
  A detailed explanation of this behavior can be found in the [section about structured attrs](@docroot@/language/advanced-attributes.md#adv-attr-structuredAttrs).

## Builder Execution

- If an output path already exists, it is removed.
  Also, locks are acquired to prevent multiple [Nix instances][Nix instance] from performing the same build at the same time.

- A log of the combined standard output and error is written to `/nix/var/log/nix`.

- The builder is executed with the arguments specified by the attribute `args`.
  If it exits with exit code 0, it is considered to have succeeded.

- The temporary directory is removed (unless the [`--keep-failed`](@docroot@/command-ref/opt-common.md#opt-keep-failed) option was specified).

## Processing outputs

There are two methods for processing outputs, as mentioned above.
But first, let us cover the requirements common to both methods.

Regardless of which method is used, each output must be turned into a valid store object.
This involves two steps:

- **Normalize the file permissions**

  The files must conform to the model described in the [Exposing in OS file systems](./file-system-object/os-file-system.md) section.
  For example, timestamps and permissions must be forced to sentinel values.

- **Calculate the references**

  Nix scans each output path for references to input store objects by looking for the store path digests of each input.
  (The name part is ignored when scanning; an input's hash part that is not followed by a `-` and the correct name part still scans as a reference.
  Likewise, a digest not preceded by the [store directory path] also still scans as a reference.)
  Since these are potential runtime dependencies, Nix will register them as references of the output store object they occur in.

### Traditional (post-build) processing

With the traditional method, the builder process on exit should have left behind files for each output the derivation is supposed to produce.
The files must be processed to turn them into bona fide store objects.
If the processing succeeds, those store objects are associated with the derivation as (the results of) a successful build.

The scanning for references that is done for each output also accounts for references from one output to another, in addition to references to the inputs.
If the outputs' references to each other form a cycle, this is an error, because the references of store objects must be acyclic.
In the case of derivations with output paths that are fixed in advance (i.e. [input-addressing] derivations, or [fixed content-addressing] derivations), the actual final store path of each output is used during the build.
For [floating content-addressing] derivations, however, the final store path is not known in advance by definition.
Scratch store paths must therefore be used instead.
Reference scanning looks for those scratch path digests, but then any output-to-be that contains such a scratch path digest must be rewritten to use the final store path digest (which is the actual valid content address) of the output in question.

In addition to output-to-output references, rewriting is also needed to support self-references in the content-addressing case.
An output may contain its own store path digest, which is a self-reference.
Hash functions which are secure cannot allow the easy calculation of the quasi-fixed points needed to support self-references "natively", so instead we replace all would-be self-references with a sentinel value, and then rewrite the sentinel value to be the final store path digest.
Superficially, this post-hashing rewriting breaks the content address, but as the self-references are easily identified, the rewriting can be inverted to yield the original hashed data, allowing verifying the content address after all.

At this point, the file system data is in the proper form, and the valid acyclic reference data for each output is also calculated, so the outputs can be registered as proper store objects, and associated with the derivation in the [build trace] in the record for a successful build.

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

[Nix instance]: @docroot@/glossary.md#gloss-nix-instance
[input-addressing]: ./derivation/outputs/input-address.md
[fixed content-addressing]: ./derivation/outputs/content-address.md#fixed
[floating content-addressing]: ./derivation/outputs/content-address.md#floating
[build trace]: ./build-trace.md
