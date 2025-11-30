# Building

As discussed in the [main page on derivations](./derivation/index.md):

> A derivation is a specification for running an executable on precisely defined input to produce one or more [store objects][store object].

This page describes *building* a derivation, which is to say following the instructions in the derivation to actually run the executable.
In some cases the derivation is self-explanatory.
For example, the arguments specified in the derivation really are the arguments passed to the executable.
In other cases, however, there is additional procedure true for all derivations, which is therefore *not* specified in the derivation.
This page specifies this invariant procedure true for all derivations, too.

The life cycle of a build can be broken down into 3 parts:

1. Preparing the environment the builder will run in, including preparing the inputs and other file system state as exposed to the builder process.

2. Running the builder

3. Processing the inputs after the builder has exited.

## Preparing the environment

### Inputs

Only [*resolved*](./resolution.md) derivations can be built.
That is to day, a derivation that depends on other derivations is not ready yet to be built, because those other derivations might not be built.
If the other derivations are indeed built, we can witness this fact by resolving the derivation, and making all the derivations inputs store paths.

> **Note**
>
> Note that [input-addressing](derivation/outputs/input-address.md) derivations are improperly resolved.
> As discussed on the linked page, the current input-addressing algorithm does not respect resolution-equivalence of derivations (\\(\\sim_\mathrm{Drv}\\)).
> That means that if Nix properly resolved an input-addressed derivation, the resolved derivation would have different input addresses, violating expectations.
> Nix therefore improperly resolves the derivation, keeping its original input address output paths, creating an invalid derivation that is both resolved and instructed to create the outputs at the originally expected paths.

The builder will be run against a file system in which the [closure] of the inputs is mounted inside the [store directory].
In particular, consider a store that just contains this closure.
That store may be exposed to the file system according to the rules specified in the [store directory](@docroot@/store/store-path.md#store-directory) documentation.
This precisely defines the file system layout of the store that should be visible to the builder process.

> **Note**
>
> Historically, Nix exposed *at least* the following store contents to the builder, but also arbitrarily other store objects, due to limitations around operating systems' file system virtualization capabilities, and wanting to avoid copying or moving files.
> It still can do this in so-called *unsandboxed* builds.
>
> Such builds should be considered an unsafe extension, but one that works less badly against non-malicious derivations than might be expected.
> This is because store paths are relatively unpredictable, so a well-behavior program is unlikely to stumble upon a store object it wasn't supposed to know about.
>
> As operating systems developed more better file system primitives, the need for disabling sandboxing has lessened greatly over the years, and this trend should continue into the future.

[realised]: @docroot@/glossary.md#gloss-realise
[closure]: @docroot@/glossary.md#gloss-closure
[store directory]: @docroot@/store/store-path.md

### Other file system state

- The current working directory of the builder process will be a fresh temporary directory that is initially empty.

- A temporary directory is created under the directory specified by
  `TMPDIR` (default `/tmp`) where the build will take place. The
  current directory is changed to this directory.

### Environment variables


## Builder Execution {#builder-execution}

The [`builder`](./derivation/index.md#builder) is executed as follows:

- The environment is cleared and set to the derivation attributes, as
  specified above.

- In addition, the following variables are set:

  - `NIX_BUILD_TOP` contains the path of the temporary directory for
    this build.

  - Also, `TMPDIR`, `TEMPDIR`, `TMP`, `TEMP` are set to point to the
    temporary directory. This is to prevent the builder from
    accidentally writing temporary files anywhere else. Doing so
    might cause interference by other processes.

  - `PATH` is set to `/path-not-set` to prevent shells from
    initialising it to their built-in default value.

  - `HOME` is set to `/homeless-shelter` to prevent programs from
    using `/etc/passwd` or the like to find the user's home
    directory, which could cause impurity. Usually, when `HOME` is
    set, it is used as the location of the home directory, even if
    it points to a non-existent path.

  - `NIX_STORE` is set to the path of the top-level Nix store
    directory (typically, `/nix/store`).

  - `NIX_ATTRS_JSON_FILE` & `NIX_ATTRS_SH_FILE` if `__structuredAttrs`
    is set to `true` for the derivation. A detailed explanation of this
    behavior can be found in the
    [section about structured attrs](@docroot@/language/advanced-attributes.md#adv-attr-structuredAttrs).

  - For each output declared in `outputs`, the corresponding
    environment variable is set to point to the intended path in the
    Nix store for that output. Each output path is a concatenation
    of the cryptographic hash of all build inputs, the `name`
    attribute and the output name. (The output name is omitted if
    it’s `out`.)

- If an output path already exists, it is removed. Also, locks are
  acquired to prevent multiple [Nix instances][Nix instance] from performing the same
  build at the same time.

- A log of the combined standard output and error is written to
  `/nix/var/log/nix`.

- The builder is executed with the arguments specified by the
  attribute `args`. If it exits with exit code 0, it is considered to
  have succeeded.

- The temporary directory is removed (unless the `-K` option was
  specified).

## Processing outputs

If the builder exited successfully, the following steps happen in order to turn the output directories left behind by the builder into proper store objects:

- **Normalize the file permissions**

  Nix sets the last-modified timestamp on all files
  in the build result to 1 (00:00:01 1/1/1970 UTC), sets the group to
  the default group, and sets the mode of the file to 0444 or 0555
  (i.e., read-only, with execute permission enabled if the file was
  originally executable). Any possible `setuid` and `setgid`
  bits are cleared.

  > **Note**
  >
  > Setuid and setgid programs are not currently supported by Nix.
  > This is because the Nix archives used in deployment have no concept of ownership information,
  > and because it makes the build result dependent on the user performing the build.

- **Calculate the references**

  Nix scans each output path for
  references to input paths by looking for the hash parts of the input
  paths. Since these are potential runtime dependencies, Nix registers
  them as dependencies of the output paths.

  Nix also scans for references to other outputs' paths in the same way, because outputs are allowed to refer to each other.
  If the outputs' references to each other form a cycle, this is an error, because the references of store objects much be acyclic.


[Nix instance]: @docroot@/glossary.md#gloss-nix-instance
