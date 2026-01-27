# Derivations

The most important built-in function is `derivation`, which is used to describe a single store-layer [store derivation].
Consult the [store chapter](@docroot@/store/derivation/index.md) for what a store derivation is;
this section just concerns how to create one from the Nix language.

This builtin function takes as input an attribute set, the attributes of which specify the inputs to the process.
It outputs an attribute set, and produces a [store derivation] as a side effect of evaluation.

[store derivation]: @docroot@/glossary.md#gloss-store-derivation

## Input attributes

### Required

- [`name`]{#attr-name} ([String](@docroot@/language/types.md#type-string))

  A symbolic name for the derivation.
  See [derivation outputs](@docroot@/store/derivation/outputs/index.md#outputs) for what this is affects.

  [store path]: @docroot@/store/store-path.md

  > **Example**
  >
  > ```nix
  > derivation {
  >   name = "hello";
  >   # ...
  > }
  > ```
  >
  > The derivation's path will be `/nix/store/<hash>-hello.drv`.
  > The [output](#attr-outputs) paths will be of the form `/nix/store/<hash>-hello[-<output>]`

- [`system`]{#attr-system} ([String](@docroot@/language/types.md#type-string))

  See [system](@docroot@/store/derivation/index.md#system).

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
  > [`builtins.currentSystem`](@docroot@/language/builtins.md#builtins-currentSystem) has the value of the [`system` configuration option], and defaults to the system type of the current Nix installation.

- [`builder`]{#attr-builder} ([Path](@docroot@/language/types.md#type-path) | [String](@docroot@/language/types.md#type-string))

  See [builder](@docroot@/store/derivation/index.md#builder).

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

- [`args`]{#attr-args} ([List](@docroot@/language/types.md#type-list) of [String](@docroot@/language/types.md#type-string))

  Default: `[ ]`

  See [args](@docroot@/store/derivation/index.md#args).

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

- [`outputs`]{#attr-outputs} ([List](@docroot@/language/types.md#type-list) of [String](@docroot@/language/types.md#type-string))

  Default: `[ "out" ]`

  Symbolic outputs of the derivation.
  Each output name is passed to the [`builder`](#attr-builder) executable as an environment variable with its value set to the corresponding [store path].

  By default, a derivation produces a single output called `out`.
  However, derivations can produce multiple outputs.
  This allows the associated [store objects](@docroot@/store/store-object.md) and their [closures](@docroot@/glossary.md#gloss-closure) to be copied or garbage-collected separately.

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
