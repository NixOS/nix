# Advanced Attributes

Derivations can declare some infrequently used optional attributes.

## Inputs

  - [`exportReferencesGraph`]{#adv-attr-exportReferencesGraph}\
    This attribute allows builders access to the references graph of
    their inputs. The attribute is a list of inputs in the Nix store
    whose references graph the builder needs to know. The value of
    this attribute should be a list of pairs `[ name1 path1 name2
    path2 ...  ]`. The references graph of each *pathN* will be stored
    in a text file *nameN* in the temporary build directory. The text
    files have the format used by `nix-store --register-validity`
    (with the deriver fields left empty). For example, when the
    following derivation is built:

    ```nix
    derivation {
      ...
      exportReferencesGraph = [ "libfoo-graph" libfoo ];
    };
    ```

    the references graph of `libfoo` is placed in the file
    `libfoo-graph` in the temporary build directory.

    `exportReferencesGraph` is useful for builders that want to do
    something with the closure of a store path. Examples include the
    builders in NixOS that generate the initial ramdisk for booting
    Linux (a `cpio` archive containing the closure of the boot script)
    and the ISO-9660 image for the installation CD (which is populated
    with a Nix store containing the closure of a bootable NixOS
    configuration).

  - [`passAsFile`]{#adv-attr-passAsFile}\
    A list of names of attributes that should be passed via files rather
    than environment variables. For example, if you have

    ```nix
    passAsFile = ["big"];
    big = "a very long string";
    ```

    then when the builder runs, the environment variable `bigPath`
    will contain the absolute path to a temporary file containing `a
    very long string`. That is, for any attribute *x* listed in
    `passAsFile`, Nix will pass an environment variable `xPath`
    holding the path of the file containing the value of attribute
    *x*. This is useful when you need to pass large strings to a
    builder, since most operating systems impose a limit on the size
    of the environment (typically, a few hundred kilobyte).

  - [`__structuredAttrs`]{#adv-attr-structuredAttrs}\
    If the special attribute `__structuredAttrs` is set to `true`, the other derivation
    attributes are serialised into a file in JSON format.

    This obviates the need for [`passAsFile`](#adv-attr-passAsFile) since JSON files have no size restrictions, unlike process environments.
    It also makes it possible to tweak derivation settings in a structured way;
    see [`outputChecks`](#adv-attr-outputChecks) for example.

    See the [corresponding section in the derivation page](@docroot@/store/derivation/index.md#structured-attrs) for further details.

    > **Warning**
    >
    > If set to `true`, other advanced attributes such as [`allowedReferences`](#adv-attr-allowedReferences), [`allowedRequisites`](#adv-attr-allowedRequisites),
    [`disallowedReferences`](#adv-attr-disallowedReferences) and [`disallowedRequisites`](#adv-attr-disallowedRequisites), maxSize, and maxClosureSize.
    will have no effect.

## Output checks

See the [corresponding section in the derivation output page](@docroot@/store/derivation/outputs/index.md).

  - [`allowedReferences`]{#adv-attr-allowedReferences}\
    The optional attribute `allowedReferences` specifies a list of legal
    references (dependencies) of the output of the builder. For example,

    ```nix
    allowedReferences = [];
    ```

    enforces that the output of a derivation cannot have any runtime
    dependencies on its inputs. To allow an output to have a runtime
    dependency on itself, use `"out"` as a list item. This is used in
    NixOS to check that generated files such as initial ramdisks for
    booting Linux don’t have accidental dependencies on other paths in
    the Nix store.

  - [`allowedRequisites`]{#adv-attr-allowedRequisites}\
    This attribute is similar to `allowedReferences`, but it specifies
    the legal requisites of the whole closure, so all the dependencies
    recursively. For example,

    ```nix
    allowedRequisites = [ foobar ];
    ```

    enforces that the output of a derivation cannot have any other
    runtime dependency than `foobar`, and in addition it enforces that
    `foobar` itself doesn't introduce any other dependency itself.

  - [`disallowedReferences`]{#adv-attr-disallowedReferences}\
    The optional attribute `disallowedReferences` specifies a list of
    illegal references (dependencies) of the output of the builder. For
    example,

    ```nix
    disallowedReferences = [ foo ];
    ```

    enforces that the output of a derivation cannot have a direct
    runtime dependencies on the derivation `foo`.

  - [`disallowedRequisites`]{#adv-attr-disallowedRequisites}\
    This attribute is similar to `disallowedReferences`, but it
    specifies illegal requisites for the whole closure, so all the
    dependencies recursively. For example,

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

    - `maxSize` defines the maximum size of the resulting [store object](@docroot@/store/store-object.md).
    - `maxClosureSize` defines the maximum size of the output's closure.
    - `ignoreSelfRefs` controls whether self-references should be considered when
      checking for allowed references/requisites.

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
    If set to `true`, it disables scanning the output for runtime dependencies.

    Example:

    ```nix
    __structuredAttrs = true;
    unsafeDiscardReferences.out = true;
    ```

    This is useful, for example, when generating self-contained filesystem images with
    their own embedded Nix store: hashes found inside such an image refer
    to the embedded store and not to the host's Nix store.

## Build scheduling

  - [`preferLocalBuild`]{#adv-attr-preferLocalBuild}\
    If this attribute is set to `true` and [distributed building is enabled](@docroot@/command-ref/conf-file.md#conf-builders), then, if possible, the derivation will be built locally instead of being forwarded to a remote machine.
    This is useful for derivations that are cheapest to build locally.

  - [`allowSubstitutes`]{#adv-attr-allowSubstitutes}\
    If this attribute is set to `false`, then Nix will always build this derivation (locally or remotely); it will not try to substitute its outputs.
    This is useful for derivations that are cheaper to build than to substitute.

    This attribute can be ignored by setting [`always-allow-substitutes`](@docroot@/command-ref/conf-file.md#conf-always-allow-substitutes) to `true`.

    > **Note**
    >
    > If set to `false`, the [`builder`] should be able to run on the system type specified in the [`system` attribute](./derivations.md#attr-system), since the derivation cannot be substituted.

    [`builder`]: ./derivations.md#attr-builder

- [`requiredSystemFeatures`]{#adv-attr-requiredSystemFeatures}\
  If a derivation has the `requiredSystemFeatures` attribute, then Nix will only build it on a machine that has the corresponding features set in its [`system-features` configuration](@docroot@/command-ref/conf-file.md#conf-system-features).

  For example, setting

  ```nix
  requiredSystemFeatures = [ "kvm" ];
  ```

  ensures that the derivation can only be built on a machine with the `kvm` feature.

# Impure builder configuration

  - [`impureEnvVars`]{#adv-attr-impureEnvVars}\
    This attribute allows you to specify a list of environment variables
    that should be passed from the environment of the calling user to
    the builder. Usually, the environment is cleared completely when the
    builder is executed, but with this attribute you can allow specific
    environment variables to be passed unmodified. For example,
    `fetchurl` in Nixpkgs has the line

    ```nix
    impureEnvVars = [ "http_proxy" "https_proxy" ... ];
    ```

    to make it use the proxy server configuration specified by the user
    in the environment variables `http_proxy` and friends.

    This attribute is only allowed in [fixed-output derivations][fixed-output derivation],
    where impurities such as these are okay since (the hash
    of) the output is known in advance. It is ignored for all other
    derivations.

    > **Warning**
    >
    > `impureEnvVars` implementation takes environment variables from
    > the current builder process. When a daemon is building its
    > environmental variables are used. Without the daemon, the
    > environmental variables come from the environment of the
    > `nix-build`.

    If the [`configurable-impure-env` experimental
    feature](@docroot@/development/experimental-features.md#xp-feature-configurable-impure-env)
    is enabled, these environment variables can also be controlled
    through the
    [`impure-env`](@docroot@/command-ref/conf-file.md#conf-impure-env)
    configuration setting.

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

    The `outputHash` attribute must be a string containing the hash in either hexadecimal or "nix32" encoding, or following the format for integrity metadata as defined by [SRI](https://www.w3.org/TR/SRI/).
    The "nix32" encoding is an adaptation of base-32 encoding.

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
