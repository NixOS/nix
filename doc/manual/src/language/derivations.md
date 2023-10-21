# Derivations

The most important built-in function is `derivation`, which is used to describe a single derivation:
a specification for running an executable on precisely defined input files to repeatably produce output files at uniquely determined file system paths.

It takes as input an attribute set, the attributes of which specify the inputs to the process.
It outputs an attribute set, and produces a [store derivation] as a side effect of evaluation.

[store derivation]: @docroot@/glossary.md#gloss-store-derivation

## Input attributes

### Required

- [`name`]{#attr-name} ([String](@docroot@/language/values.md#type-string))

  A symbolic name for the derivation.
  It is added to the [store path] of the corresponding [store derivation] as well as to its [output paths](@docroot@/glossary.md#gloss-output-path).

  [store path]: @docroot@/glossary.md#gloss-store-path

  > **Example**
  >
  > ```nix
  > derivation {
  >   name = "hello";
  >   # ...
  > }
  > ```
  >
  > The store derivation's path will be `/nix/store/<hash>-hello.drv`.
  > The [output](#attr-outputs) paths will be of the form `/nix/store/<hash>-hello[-<output>]`

- [`system`]{#attr-system} ([String](@docroot@/language/values.md#type-string))

  The system type on which the [`builder`](#attr-builder) executable is meant to be run.

  A necessary condition for Nix to build derivations locally is that the `system` attribute matches the current [`system` configuration option].
  It can automatically [build on other platforms](@docroot@/language/derivations.md#attr-builder) by forwarding build requests to other machines.

  [`system` configuration option]: @docroot@/command-ref/conf-file.md#conf-system

  > **Example**
  >
  > Declare a derivation to be built on a specific system type:
  >
  > ```nix
  > derivation {
  >   # ...
  >   system = "x86_64-linux";
  >   # ...
  > }
  > ```

  > **Example**
  >
  > Declare a derivation to be built on the system type that evaluates the expression:
  >
  > ```nix
  > derivation {
  >   # ...
  >   system = builtins.currentSystem;
  >   # ...
  > }
  > ```
  >
  > [`builtins.currentSystem`](@docroot@/language/builtin-constants.md#builtins-currentSystem) has the value of the [`system` configuration option], and defaults to the system type of the current Nix installation.

- [`builder`]{#attr-builder} ([Path](@docroot@/language/values.md#type-path) | [String](@docroot@/language/values.md#type-string))

  Path to an executable that will perform the build.

  > **Example**
  >
  > Use the file located at `/bin/bash` as the builder executable:
  >
  > ```nix
  > derivation {
  >   # ...
  >   builder = "/bin/bash";
  >   # ...
  > };
  > ```

  <!-- -->

  > **Example**
  >
  > Copy a local file to the Nix store for use as the builder executable:
  >
  > ```nix
  > derivation {
  >   # ...
  >   builder = ./builder.sh;
  >   # ...
  > };
  > ```

  <!-- -->

  > **Example**
  >
  > Use a file from another derivation as the builder executable:
  >
  > ```nix
  > let pkgs = import <nixpkgs> {}; in
  > derivation {
  >   # ...
  >   builder = "${pkgs.python}/bin/python";
  >   # ...
  > };
  > ```

### Optional

- [`args`]{#attr-args} ([List](@docroot@/language/values.md#list) of [String](@docroot@/language/values.md#type-string))

  Default: `[ ]`

  Command-line arguments to be passed to the [`builder`](#attr-builder) executable.

  > **Example**
  >
  > Pass arguments to Bash to interpret a shell command:
  >
  > ```nix
  > derivation {
  >   # ...
  >   builder = "/bin/bash";
  >   args = [ "-c" "echo hello world > $out" ];
  >   # ...
  > };
  > ```

- [`outputs`]{#attr-outputs} ([List](@docroot@/language/values.md#list) of [String](@docroot@/language/values.md#type-string))

  Default: `[ "out" ]`

  Symbolic outputs of the derivation.
  Each output name is passed to the [`builder`](#attr-builder) executable as an environment variable with its value set to the corresponding [store path].

  By default, a derivation produces a single output called `out`.
  However, derivations can produce multiple outputs.
  This allows the associated [store objects](@docroot@/glossary.md#gloss-store-object) and their [closures](@docroot@/glossary.md#gloss-closure) to be copied or garbage-collected separately.

  > **Example**
  >
  > Imagine a library package that provides a dynamic library, header files, and documentation.
  > A program that links against such a library doesn’t need the header files and documentation at runtime, and it doesn’t need the documentation at build time.
  > Thus, the library package could specify:
  >
  > ```nix
  > derivation {
  >   # ...
  >   outputs = [ "lib" "dev" "doc" ];
  >   # ...
  > }
  > ```
  >
  > This will cause Nix to pass environment variables `lib`, `dev`, and `doc` to the builder containing the intended store paths of each output.
  > The builder would typically do something like
  >
  > ```bash
  > ./configure \
  >   --libdir=$lib/lib \
  >   --includedir=$dev/include \
  >   --docdir=$doc/share/doc
  > ```
  >
  > for an Autoconf-style package.

  The name of an output is combined with the name of the derivation to create the name part of the output's store path, unless it is `out`, in which case just the name of the derivation is used.

  > **Example**
  >
  >
  > ```nix
  > derivation {
  >   name = "example";
  >   outputs = [ "lib" "dev" "doc" "out" ];
  >   # ...
  > }
  > ```
  >
  > The store derivation path will be `/nix/store/<hash>-example.drv`.
  > The output paths will be
  > - `/nix/store/<hash>-example-lib`
  > - `/nix/store/<hash>-example-dev`
  > - `/nix/store/<hash>-example-doc`
  > - `/nix/store/<hash>-example`

  You can refer to each output of a derivation by selecting it as an attribute.
  The first element of `outputs` determines the *default output* and ends up at the top-level.

  > **Example**
  >
  > Select an output by attribute name:
  >
  > ```nix
  > let
  >   myPackage = derivation {
  >     name = "example";
  >     outputs = [ "lib" "dev" "doc" "out" ];
  >     # ...
  >   };
  > in myPackage.dev
  > ```
  >
  > Since `lib` is the first output, `myPackage` is equivalent to `myPackage.lib`.

  <!-- FIXME: refer to the output attributes when we have one -->

- See [Advanced Attributes](./advanced-attributes.md) for more, infrequently used, optional attributes.

  <!-- FIXME: This should be moved here -->

- Every other attribute is passed as an environment variable to the builder.
  Attribute values are translated to environment variables as follows:

    - Strings are passed unchanged.

    - Integral numbers are converted to decimal notation.

    - Floating point numbers are converted to simple decimal or scientific notation with a preset precision.

    - A *path* (e.g., `../foo/sources.tar`) causes the referenced file
      to be copied to the store; its location in the store is put in
      the environment variable. The idea is that all sources should
      reside in the Nix store, since all inputs to a derivation should
      reside in the Nix store.

    - A *derivation* causes that derivation to be built prior to the
      present derivation. The environment variable is set to the [store path] of the derivation's default [output](#attr-outputs).

    - Lists of the previous types are also allowed. They are simply
      concatenated, separated by spaces.

    - `true` is passed as the string `1`, `false` and `null` are
      passed as an empty string.

<!-- FIXME: add a section on output attributes -->

## Builder execution

The [`builder`](#attr-builder) is executed as follows:

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
    [section about structured attrs](./advanced-attributes.md#adv-attr-structuredAttrs).

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

- If the build was successful, Nix scans each output path for
  references to input paths by looking for the hash parts of the input
  paths. Since these are potential runtime dependencies, Nix registers
  them as dependencies of the output paths.

- After the build, Nix sets the last-modified timestamp on all files
  in the build result to 1 (00:00:01 1/1/1970 UTC), sets the group to
  the default group, and sets the mode of the file to 0444 or 0555
  (i.e., read-only, with execute permission enabled if the file was
  originally executable). Note that possible `setuid` and `setgid`
  bits are cleared. Setuid and setgid programs are not currently
  supported by Nix. This is because the Nix archives used in
  deployment have no concept of ownership information, and because it
  makes the build result dependent on the user performing the build.
