# Building

## Normalizing derivation inputs

- Each input must be [realised] prior to building the derivation in question.

- Once this is done, the derivation is *normalized*, replacing each input deriving path with its store path, which we now know from realising the input.

## Builder Execution

The [`builder`](./drv.md#builder) is executed as follows:

- A temporary directory is created under the directory specified by
  `TMPDIR` (default `/tmp`) where the build will take place. The
  current directory is changed to this directory.

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
  acquired to prevent multiple Nix instances from performing the same
  build at the same time.

- A log of the combined standard output and error is written to
  `/nix/var/log/nix`.

- The builder is executed with the arguments specified by the
  attribute `args`. If it exits with exit code 0, it is considered to
  have succeeded.

- The temporary directory is removed (unless the `-K` option was
  specified).

## Processing outputs and Reference scanning

- After the build, Nix sets the last-modified timestamp on all files
  in the build result to 1 (00:00:01 1/1/1970 UTC), sets the group to
  the default group, and sets the mode of the file to 0444 or 0555
  (i.e., read-only, with execute permission enabled if the file was
  originally executable). Note that possible `setuid` and `setgid`
  bits are cleared. Setuid and setgid programs are not currently
  supported by Nix. This is because the Nix archives used in
  deployment have no concept of ownership information, and because it
  makes the build result dependent on the user performing the build.

- If the build was successful, Nix scans each output path for
  references to input paths by looking for the hash parts of the input
  paths. Since these are potential runtime dependencies, Nix registers
  them as dependencies of the output paths.

TODO integrete these with the bullet

Before in the section on [store objects](./object.md), we talked abstractly about scanning for references.
Now we can make this concrete.

After the derivation's command is run, Nix needs to process the "raw" output directories to turn them into legit store objects.
There is a few steps of this, but let's start with the simple case of one input-addressed output first.

\[Overview of things that need to happen.]

For example, if Nix thinks `/nix/store/asdfasdfasdf-foo` and `/nix/store/qwerqwerqwer-bar` are paths the data might plausibly reference, Nix will scan all the contents of all files recursively for the "hash parts" `asdfasdfasdf`` and `qwerqwerqwer`.

\[Explain why whitelist.]
