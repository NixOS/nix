# Glossary

  - [derivation]{#gloss-derivation}\
    A description of a build task. The result of a derivation is a
    store object. Derivations are typically specified in Nix expressions
    using the [`derivation` primitive](./language/derivations.md). These are
    translated into low-level *store derivations* (implicitly by
    `nix-env` and `nix-build`, or explicitly by `nix-instantiate`).

    [derivation]: #gloss-derivation

  - [store derivation]{#gloss-store-derivation}\
    A [derivation] represented as a `.drv` file in the [store].
    It has a [store path], like any [store object].

    Example: `/nix/store/g946hcz4c8mdvq2g8vxx42z51qb71rvp-git-2.38.1.drv`

    See [`nix derivation show`](./command-ref/new-cli/nix3-derivation-show.md) (experimental) for displaying the contents of store derivations.

    [store derivation]: #gloss-store-derivation

  - [instantiate]{#gloss-instantiate}, instantiation\
    Translate a [derivation] into a [store derivation].

    See [`nix-instantiate`](./command-ref/nix-instantiate.md).

    [instantiate]: #gloss-instantiate

  - [realise]{#gloss-realise}, realisation\
    Ensure a [store path] is [valid][validity].

    This means either running the `builder` executable as specified in the corresponding [derivation] or fetching a pre-built [store object] from a [substituter].

    See [`nix-build`](./command-ref/nix-build.md) and [`nix-store --realise`](@docroot@/command-ref/nix-store/realise.md).

    See [`nix build`](./command-ref/new-cli/nix3-build.md) (experimental).

    [realise]: #gloss-realise

  - [content-addressed derivation]{#gloss-content-addressed-derivation}\
    A derivation which has the
    [`__contentAddressed`](./language/advanced-attributes.md#adv-attr-__contentAddressed)
    attribute set to `true`.

  - [fixed-output derivation]{#gloss-fixed-output-derivation}\
    A derivation which includes the
    [`outputHash`](./language/advanced-attributes.md#adv-attr-outputHash) attribute.

  - [store]{#gloss-store}\
    The location in the file system where store objects live. Typically
    `/nix/store`.

    From   the  perspective   of   the  location   where  Nix   is
    invoked, the  Nix store can be  referred to
    as a "_local_" or a "_remote_" one:

    + A [local store]{#gloss-local-store} exists on the filesystem of
      the machine where Nix is  invoked. You can use other
      local stores  by passing  the `--store` flag  to the
      `nix` command.  Local stores can be used for building derivations.

    + A  *remote store*  exists  anywhere  other than  the
      local  filesystem. One  example is  the `/nix/store`
      directory on another machine,  accessed via `ssh` or
      served by the `nix-serve` Perl script.

    [store]: #gloss-store
    [local store]: #gloss-local-store

  - [chroot store]{#gloss-chroot-store}\
    A [local store] whose canonical path is anything other than `/nix/store`.

  - [binary cache]{#gloss-binary-cache}\
    A *binary cache* is a Nix store which uses a different format: its
    metadata and signatures are kept in `.narinfo` files rather than in a
    [Nix database]. This different format simplifies serving store objects
    over the network, but cannot host builds. Examples of binary caches
    include S3 buckets and the [NixOS binary cache](https://cache.nixos.org).

  - [store path]{#gloss-store-path}\
    The location of a [store object] in the file system, i.e., an
    immediate child of the Nix store directory.

    Example: `/nix/store/a040m110amc4h71lds2jmr8qrkj2jhxd-git-2.38.1`

    [store path]: #gloss-store-path

  - [file system object]{#gloss-store-object}\
    The Nix data model for representing simplified file system data.

    See [File System Object](@docroot@/architecture/file-system-object.md) for details.

    [file system object]: #gloss-file-system-object

  - [store object]{#gloss-store-object}\

    A store object consists of a [file system object], [reference]s to other store objects, and other metadata.
    It can be referred to by a [store path].

    [store object]: #gloss-store-object

  - [input-addressed store object]{#gloss-input-addressed-store-object}\
    A store object produced by building a
    non-[content-addressed](#gloss-content-addressed-derivation),
    non-[fixed-output](#gloss-fixed-output-derivation)
    derivation.

  - [output-addressed store object]{#gloss-output-addressed-store-object}\
    A [store object] whose [store path] is determined by its contents.
    This includes derivations, the outputs of [content-addressed derivations](#gloss-content-addressed-derivation), and the outputs of [fixed-output derivations](#gloss-fixed-output-derivation).

  - [substitute]{#gloss-substitute}\
    A substitute is a command invocation stored in the [Nix database] that
    describes how to build a store object, bypassing the normal build
    mechanism (i.e., derivations). Typically, the substitute builds the
    store object by downloading a pre-built version of the store object
    from some server.

  - [substituter]{#gloss-substituter}\
    An additional [store]{#gloss-store} from which Nix can obtain store objects instead of building them.
    Often the substituter is a [binary cache](#gloss-binary-cache), but any store can serve as substituter.

    See the [`substituters` configuration option](./command-ref/conf-file.md#conf-substituters) for details.

    [substituter]: #gloss-substituter

  - [purity]{#gloss-purity}\
    The assumption that equal Nix derivations when run always produce
    the same output. This cannot be guaranteed in general (e.g., a
    builder can rely on external inputs such as the network or the
    system time) but the Nix model assumes it.

  - [Nix database]{#gloss-nix-database}\
    An SQlite database to track [reference]s between [store object]s.
    This is an implementation detail of the [local store].

    Default location: `/nix/var/nix/db`.

    [Nix database]: #gloss-nix-database

  - [Nix expression]{#gloss-nix-expression}\
    A high-level description of software packages and compositions
    thereof. Deploying software using Nix entails writing Nix
    expressions for your packages. Nix expressions are translated to
    derivations that are stored in the Nix store. These derivations can
    then be built.

  - [reference]{#gloss-reference}\
    A [store object] `O` is said to have a *reference* to a store object `P` if a [store path] to `P` appears in the contents of `O`.

    Store objects can refer to both other store objects and themselves.
    References from a store object to itself are called *self-references*.
    References other than a self-reference must not form a cycle.

    [reference]: #gloss-reference

  - [reachable]{#gloss-reachable}\
    A store path `Q` is reachable from another store path `P` if `Q`
    is in the *closure* of the *references* relation.

  - [closure]{#gloss-closure}\
    The closure of a store path is the set of store paths that are
    directly or indirectly “reachable” from that store path; that is,
    it’s the closure of the path under the *references* relation. For
    a package, the closure of its derivation is equivalent to the
    build-time dependencies, while the closure of its output path is
    equivalent to its runtime dependencies. For correct deployment it
    is necessary to deploy whole closures, since otherwise at runtime
    files could be missing. The command `nix-store --query --requisites ` prints out
    closures of store paths.

    As an example, if the [store object] at path `P` contains a [reference]
    to a store object at path `Q`, then `Q` is in the closure of `P`. Further, if `Q`
    references `R` then `R` is also in the closure of `P`.

    [closure]: #gloss-closure

  - [output path]{#gloss-output-path}\
    A [store path] produced by a [derivation].

    [output path]: #gloss-output-path

  - [deriver]{#gloss-deriver}\
    The [store derivation] that produced an [output path].

  - [validity]{#gloss-validity}\
    A store path is valid if all [store object]s in its [closure] can be read from the [store].

    For a [local store], this means:
    - The store path leads to an existing [store object] in that [store].
    - The store path is listed in the [Nix database] as being valid.
    - All paths in the store path's [closure] are valid.

    [validity]: #gloss-validity

  - [user environment]{#gloss-user-env}\
    An automatically generated store object that consists of a set of
    symlinks to “active” applications, i.e., other store paths. These
    are generated automatically by
    [`nix-env`](./command-ref/nix-env.md). See *profiles*.

  - [profile]{#gloss-profile}\
    A symlink to the current *user environment* of a user, e.g.,
    `/nix/var/nix/profiles/default`.

  - [installable]{#gloss-installable}\
    Something that can be realised in the Nix store.

    See [installables](./command-ref/new-cli/nix.md#installables) for [`nix` commands](./command-ref/new-cli/nix.md) (experimental) for details.

  - [platform]{#gloss-platform}

    [platform]: #gloss-platform

    A set of execution environments capable of executing some set of software. A platform is typically described by attributes such as instruction set architecture, operating system, and application binary interface.

    Nix is not intimately aware of all possible platforms.
    Rather, it abstracts the platform into
     - [system type]{#gloss-system-type} string
     - [system features](command-ref/conf-file.md#system-features), to a lesser extent, as these are often thought of as additions to a platform, rather being part of the platform definition.

  - [system type]:{#gloss-system-type}

    An identifier for a specific [platform] or (in rare cases) a class of compatible platforms, such as `x86_64-v3-linux`.

    To build a derivation locally, the [derivation `system` field](language/derivations.md#attr-system) must occur in the local Nix installation's [`system`](command-ref/conf-file.md#conf-system) or [`extra-platforms`](command-ref/conf-file.md#conf-extra-platforms) settings, among other rules.
    Otherwise, a [remote build](advanced-topics/distributed-builds.md) is needed.

  - [build platform]{#gloss-build-platform}

    [build platform]: #gloss-build-platform

    The [platform] that performs the build or compilation, typically a dash-separated string.

    A program that is produced by a [derivation] has a [build configuration] with a [build platform] that can be executed by the [derivation system](language/derivations.md#attr-system).
    After it has been built, the [build platform] is no longer relevant.

    This term is based on the [GCC system terminology](#gloss-gcc-system-terminology).

  - [host platform]{#gloss-host-platform}

    [host platform]: #gloss-host-platform

    The [platform] where build products will run on, typically a dash-separated string.

    Programs that run natively in a [derivation] must have a [build configuration] with a [host platform] that can by executed by the [derivation system](language/derivations.md#attr-system).

    This term is based on the [GCC system terminology](#gloss-gcc-system-terminology).

  - [target platform]{#gloss-target-platform}

    [target platform]: #gloss-target-platform

    The [platform] for which a compiler will generate code, typically a dash-separated string.

    When discussing a build that does not produce a compiler, we only use [build platform] and [host platform] to describe it. This also applies to compilers with a dynamic target, which is not part of the [build configuration].

    We try to avoid discussing the actual build from the perspective of its compiler, instead focusing on the build itself.
    Otherwise we won't have a term to distinguish the actual [target platform] from what we should have called the [host platform].

    If the output of a compiler is to be run in a [derivation], the compiler's [target platform] must be executable by the [derivation system](language/derivations.md#attr-system).

    This term is based on the [GCC system terminology](#gloss-gcc-system-terminology).

  - [GCC system terminology]{#gloss-gcc-system-terminology}

    The Nix ecosystem uses the [GCC system terminology](https://gcc.gnu.org/onlinedocs/gccint/Configure-Terms.html) for the names and roles of _build_, _host_, and _target_, but may apply the term _cross_ more loosely.

    See [host platform], [build platform] and [target platform] for how each relates to [derivation].

  - [build configuration]{#gloss-build-configuration}

    [build configuration]: #gloss-build-configuration

    The software configuration whose effects enable the build process and most of which also influence the [build product](#gloss-build-product).

    Build configuration does not have a single agreed upon definition, but consists of a potentially broad set of settings that include:
    - [build platform], often implicitly
    - [host platform]
    - [target platform], only when building a compiler with a static target platform
    - options passed to a compiler, such optimization settings and the host platform (when using a compiler with a dynamic target platform)
    - dependency options, such as which build tools and dependencies to use
    - anything specified in a custom build script

    In the context of Nix, the [build configuration] can be thought of as a [derivation] minus the source code, plus intentional build impurities.

  - [build product]{#gloss-build-product}

    The result of running a build process. When a build process runs in a [derivation], build products are typically copied into the [output path](#gloss-output-path)(s).

  - [NAR]{#gloss-nar}\
    A *N*ix *AR*chive. This is a serialisation of a path in the Nix
    store. It can contain regular files, directories and symbolic
    links.  NARs are generated and unpacked using `nix-store --dump`
    and `nix-store --restore`.

  - [`∅`]{#gloss-emtpy-set}\
    The empty set symbol. In the context of profile history, this denotes a package is not present in a particular version of the profile.

  - [`ε`]{#gloss-epsilon}\
    The epsilon symbol. In the context of a package, this means the version is empty. More precisely, the derivation does not have a version attribute.

  - [string interpolation]{#gloss-string-interpolation}\
    Expanding expressions enclosed in `${ }` within a [string], [path], or [attribute name].

    See [String interpolation](./language/string-interpolation.md) for details.

    [string]: ./language/values.md#type-string
    [path]: ./language/values.md#type-path
    [attribute name]: ./language/values.md#attribute-set

  - [experimental feature]{#gloss-experimental-feature}\
    Not yet stabilized functionality guarded by named experimental feature flags.
    These flags are enabled or disabled with the [`experimental-features`](./command-ref/conf-file.html#conf-experimental-features) setting.

    See the contribution guide on the [purpose and lifecycle of experimental feaures](@docroot@/contributing/experimental-features.md).
