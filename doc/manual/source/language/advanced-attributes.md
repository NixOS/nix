# Advanced Attributes

Derivations can declare some infrequently used optional attributes.

## Inputs

  - [`exportReferencesGraph`]{#adv-attr-exportReferencesGraph}\
    Specifies the [*export references graph*](@docroot@/store/derivation/index.md#export-references-graph) option.
    The value of this attribute should be a list of pairs `[ name1 path1 name2 path2 ...  ]`.
    For example, when the following derivation is built:

    ```nix
    derivation {
      ...
      exportReferencesGraph = [ "libfoo-graph" libfoo ];
    };
    ```

    the references graph of `libfoo` is placed in the file
    `libfoo-graph` in the temporary build directory.

  - [`passAsFile`]{#adv-attr-passAsFile}\
    A list of names of attributes whose environment variables should be marked
    [*pass as file*](@docroot@/store/derivation/index.md#pass-as-file).
    For example, if you have

    ```nix
    passAsFile = ["big"];
    big = "a very long string";
    ```

    then when the builder runs, the environment variable `bigPath`
    will contain the absolute path to a temporary file containing `a
    very long string`.

  - [`__structuredAttrs`]{#adv-attr-structuredAttrs}\
    If the special attribute `__structuredAttrs` is set to `true`, the other derivation
    attributes are serialised into a file in JSON format.

    See the [corresponding section in the derivation page](@docroot@/store/derivation/index.md#structured-attrs) for further details.

    > **Warning**
    >
    > If set to `true`, the top-level attributes [`allowedReferences`](#adv-attr-allowedReferences), [`allowedRequisites`](#adv-attr-allowedRequisites),
    > [`disallowedReferences`](#adv-attr-disallowedReferences) and [`disallowedRequisites`](#adv-attr-disallowedRequisites)
    > will have no effect; use [`outputChecks`](#adv-attr-outputChecks) instead.

## Output checks

These attributes specify [checks on the derivation's outputs](@docroot@/store/derivation/outputs/index.md#output-checks).
The concepts behind them are documented there; this section gives the syntax for specifying them from the Nix language.

Store paths in these lists may be given as strings with context (i.e. by interpolating a derivation), and outputs of the derivation itself are denoted by their plain output names, e.g. `"out"`.

  - [`allowedReferences`]{#adv-attr-allowedReferences}\
    Specifies the [*allowed references*](@docroot@/store/derivation/outputs/index.md#allowed-references) check. For example,

    ```nix
    allowedReferences = [];
    ```

    enforces that the output of a derivation cannot have any runtime
    dependencies on its inputs. To allow an output to have a runtime
    dependency on itself, use `"out"` as a list item.

  - [`allowedRequisites`]{#adv-attr-allowedRequisites}\
    Specifies the [*allowed requisites*](@docroot@/store/derivation/outputs/index.md#allowed-requisites) check. For example,

    ```nix
    allowedRequisites = [ foobar ];
    ```

    enforces that the output of a derivation cannot have any other
    runtime dependency than `foobar`, and in addition it enforces that
    `foobar` itself doesn't introduce any other dependency itself.

  - [`disallowedReferences`]{#adv-attr-disallowedReferences}\
    Specifies the [*disallowed references*](@docroot@/store/derivation/outputs/index.md#disallowed-references) check. For example,

    ```nix
    disallowedReferences = [ foo ];
    ```

    enforces that the output of a derivation cannot have a direct
    runtime dependency on the derivation `foo`.

  - [`disallowedRequisites`]{#adv-attr-disallowedRequisites}\
    Specifies the [*disallowed requisites*](@docroot@/store/derivation/outputs/index.md#disallowed-requisites) check. For example,

    ```nix
    disallowedRequisites = [ foobar ];
    ```

    enforces that the output of a derivation cannot have any runtime
    dependency on `foobar` or any other derivation depending recursively
    on `foobar`.

  - [`outputChecks`]{#adv-attr-outputChecks}\
    When using [structured attributes](#adv-attr-structuredAttrs), the `outputChecks`
    attribute allows defining checks per-output.

    In addition to
    [`allowedReferences`](#adv-attr-allowedReferences), [`allowedRequisites`](#adv-attr-allowedRequisites),
    [`disallowedReferences`](#adv-attr-disallowedReferences) and [`disallowedRequisites`](#adv-attr-disallowedRequisites),
    the following attributes are available:

    - `maxSize` specifies the [*max size*](@docroot@/store/derivation/outputs/index.md#max-size) check.
    - `maxClosureSize` specifies the [*max closure size*](@docroot@/store/derivation/outputs/index.md#max-closure-size) check.
    - `ignoreSelfRefs` controls the [*ignore self references*](@docroot@/store/derivation/outputs/index.md#ignore-self-refs) behavior.

    Example:

    ```nix
    __structuredAttrs = true;

    outputChecks.out = {
      # The closure of 'out' must not be larger than 256 MiB.
      maxClosureSize = 256 * 1024 * 1024;

      # It must not refer to the C compiler or to the 'dev' output.
      disallowedRequisites = [ stdenv.cc "dev" ];
    };

    outputChecks.dev = {
      # The 'dev' output must not be larger than 128 KiB.
      maxSize = 128 * 1024;
    };
    ```

## Other output modifications

  - [`unsafeDiscardReferences`]{#adv-attr-unsafeDiscardReferences}\
    When using [structured attributes](#adv-attr-structuredAttrs), the
    attribute `unsafeDiscardReferences` is an attribute set with a boolean value for each output name.
    If set to `true`, it enables the
    [*unsafe discard references*](@docroot@/store/derivation/outputs/index.md#unsafe-discard-references)
    behavior for that output.

    Example:

    ```nix
    __structuredAttrs = true;
    unsafeDiscardReferences.out = true;
    ```

## Build scheduling

  - [`preferLocalBuild`]{#adv-attr-preferLocalBuild}\
    If set to `true`, enables the [*prefer local build*](@docroot@/store/derivation/index.md#prefer-local-build) option.
    This is useful for derivations that are cheapest to build locally.

  - [`allowSubstitutes`]{#adv-attr-allowSubstitutes}\
    If set to `false`, disables the [*allow substitutes*](@docroot@/store/derivation/index.md#allow-substitutes) option, so Nix will always build this derivation (locally or remotely) rather than substituting its outputs.
    This is useful for derivations that are cheaper to build than to substitute.

- [`requiredSystemFeatures`]{#adv-attr-requiredSystemFeatures}\
  Specifies the [*required system features*](@docroot@/store/derivation/index.md#required-system-features) option:
  Nix will only build the derivation on a machine that has the corresponding features set in its [`system-features` configuration](@docroot@/command-ref/conf-file.md#conf-system-features).

  For example, setting

  ```nix
  requiredSystemFeatures = [ "kvm" ];
  ```

  ensures that the derivation can only be built on a machine with the `kvm` feature.

# Impure builder configuration

  - [`impureEnvVars`]{#adv-attr-impureEnvVars}\
    Specifies the [*impure environment variables*](@docroot@/store/derivation/index.md#impure-env-vars) option: a list of environment variables
    that should be passed from the environment of the calling user to the builder. For example,
    `fetchurl` in Nixpkgs has the line

    ```nix
    impureEnvVars = [ "http_proxy" "https_proxy" ... ];
    ```

    to make it use the proxy server configuration specified by the user
    in the environment variables `http_proxy` and friends.

## Setting the derivation type

As discussed in [Derivation Outputs and Types of Derivations](@docroot@/store/derivation/outputs/index.md), there are multiples kinds of derivations / kinds of derivation outputs.
The choice of the following attributes determines which kind of derivation we are making.

- [`__contentAddressed`]

- [`outputHash`]

- [`outputHashAlgo`]

- [`outputHashMode`]

The three types of derivations are chosen based on the following combinations of these attributes.
All other combinations are invalid.

- [Input-addressing derivations](@docroot@/store/derivation/outputs/input-address.md)

  This is the default for `builtins.derivation`.
  Nix only currently supports one kind of input-addressing, so no other information is needed.

  `__contentAddressed = false;` may also be included, but is not needed, and will trigger the experimental feature check.

- [Fixed-output derivations][fixed-output derivation]

  All of [`outputHash`], [`outputHashAlgo`], and [`outputHashMode`].

  <!--

  `__contentAddressed` is ignored, because fixed-output derivations always content-address their outputs, by definition.

  **TODO CHECK**

  -->

- [(Floating) content-addressing derivations](@docroot@/store/derivation/outputs/content-address.md)

  Both [`outputHashAlgo`] and [`outputHashMode`], `__contentAddressed = true;`, and *not* `outputHash`.

  If an output hash was given, then the derivation output would be "fixed" not "floating".

Here is more information on the `output*` attributes, and what values they may be set to:

  - [`outputHashMode`]{#adv-attr-outputHashMode}

    This specifies how the files of a content-addressing derivation output are digested to produce a content address.

    This works in conjunction with [`outputHashAlgo`](#adv-attr-outputHashAlgo).
    Specifying one without the other is an error (unless [`outputHash` is also specified and includes its own hash algorithm as described below).

    The `outputHashMode` attribute determines how the hash is computed.
    It must be one of the following values:

      - [`"flat"`](@docroot@/store/store-object/content-address.md#method-flat)

        This is the default.

      - [`"recursive"` or `"nar"`](@docroot@/store/store-object/content-address.md#method-nix-archive)

        > **Compatibility**
        >
        > `"recursive"` is the traditional way of indicating this,
        > and is supported since 2005 (virtually the entire history of Nix).
        > `"nar"` is more clear, and consistent with other parts of Nix (such as the CLI),
        > however support for it is only added in Nix version 2.21.

      - [`"text"`](@docroot@/store/store-object/content-address.md#method-text)

        > **Warning**
        >
        > The use of this method for derivation outputs is part of the [`dynamic-derivations`][xp-feature-dynamic-derivations] experimental feature.

      - [`"git"`](@docroot@/store/store-object/content-address.md#method-git)

        > **Warning**
        >
        > This method is part of the [`git-hashing`][xp-feature-git-hashing] experimental feature.

    See [content-addressing store objects](@docroot@/store/store-object/content-address.md) for more information about the process this flag controls.

  - [`outputHashAlgo`]{#adv-attr-outputHashAlgo}

    This specifies the hash algorithm used to digest the [file system object] data of a content-addressing derivation output.

    This works in conjunction with [`outputHashMode`](#adv-attr-outputHashAlgo).
    Specifying one without the other is an error (unless `outputHash` is also specified and includes its own hash algorithm as described below).

    The `outputHashAlgo` attribute specifies the hash algorithm used to compute the hash.
    It can currently be `"blake3"`, `"sha1"`, `"sha256"`, `"sha512"`, or `null`.

    `outputHashAlgo` can only be `null` when `outputHash` follows the SRI format, because in that case the choice of hash algorithm is determined by `outputHash`.

  - [`outputHash`]{#adv-attr-outputHash}

    This will specify the output hash of the single output of a [fixed-output derivation].

    The `outputHash` attribute must be a string containing the hash in either hexadecimal or "nix32" encoding, or following the format for integrity metadata as defined by [SRI](@docroot@/glossary.md#gloss-sri).
    The ["nix32" encoding](@docroot@/protocols/nix32.md) is Nix's variant of Base32 encoding.

    > **Note**
    >
    > The [`convertHash`](@docroot@/language/builtins.md#builtins-convertHash) function shows how to convert between different encodings.
    > The [`nix-hash` command](../command-ref/nix-hash.md) has information about obtaining the hash for some contents, as well as converting to and from encodings.

  - [`__contentAddressed`]{#adv-attr-__contentAddressed}

    > **Warning**
    >
    > This attribute is part of an [experimental feature](@docroot@/development/experimental-features.md).
    >
    > To use this attribute, you must enable the
    > [`ca-derivations`][xp-feature-ca-derivations] experimental feature.
    > For example, in [nix.conf](../command-ref/conf-file.md) you could add:
    >
    > ```
    > extra-experimental-features = ca-derivations
    > ```

    This is a boolean with a default of `false`.
    It determines whether the derivation is floating content-addressing.

[`__contentAddressed`]: #adv-attr-__contentAddressed
[`outputHash`]: #adv-attr-outputHash
[`outputHashAlgo`]: #adv-attr-outputHashAlgo
[`outputHashMode`]: #adv-attr-outputHashMode

[fixed-output derivation]: @docroot@/glossary.md#gloss-fixed-output-derivation
[file system object]: @docroot@/store/file-system-object.md
[store object]: @docroot@/store/store-object.md
[xp-feature-dynamic-derivations]: @docroot@/development/experimental-features.md#xp-feature-dynamic-derivations
[xp-feature-git-hashing]: @docroot@/development/experimental-features.md#xp-feature-git-hashing
