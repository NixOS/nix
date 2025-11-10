# Glossary

- [build system]{#gloss-build-system}

  Generic term for software that facilitates the building of software by automating the invocation of compilers, linkers, and other tools.

  Nix can be used as a generic build system.
  It has no knowledge of any particular programming language or toolchain.
  These details are specified in [derivation expressions](#gloss-derivation-expression).

- [content address]{#gloss-content-address}

  A
  [*content address*](https://en.wikipedia.org/wiki/Content-addressable_storage)
  is a secure way to reference immutable data.
  The reference is calculated directly from the content of the data being referenced, which means the reference is
  [*tamper proof*](https://en.wikipedia.org/wiki/Tamperproofing)
  --- variations of the data should always calculate to distinct content addresses.

  For how Nix uses content addresses, see:

    - [Content-Addressing File System Objects](@docroot@/store/file-system-object/content-address.md)
    - [Content-Addressing Store Objects](@docroot@/store/store-object/content-address.md)
    - [content-addressing derivation](#gloss-content-addressing-derivation)

  Software Heritage's writing on [*Intrinsic and Extrinsic identifiers*](https://www.softwareheritage.org/2020/07/09/intrinsic-vs-extrinsic-identifiers) is also a good introduction to the value of content-addressing over other referencing schemes.

  Besides content addressing, the Nix store also uses [input addressing](#gloss-input-addressed-store-object).

- [content-addressed storage]{#gloss-content-addressed-store}

  The industry term for storage and retrieval systems using [content addressing](#gloss-content-address). A Nix store also has [input addressing](#gloss-input-addressed-store-object), and metadata.

- [derivation]{#gloss-derivation}

  A derivation can be thought of as a [pure function](https://en.wikipedia.org/wiki/Pure_function) that produces new [store objects][store object] from existing store objects.

  Derivations are implemented as [operating system processes that run in a sandbox](@docroot@/store/building.md#builder-execution).
  This sandbox by default only allows reading from store objects specified as inputs, and only allows writing to designated [outputs][output] to be [captured as store objects](@docroot@/store/building.md#processing-outputs).

  A derivation is typically specified as a [derivation expression] in the [Nix language], and [instantiated][instantiate] to a [store derivation].
  There are multiple ways of obtaining store objects from store derivatons, collectively called [realisation][realise].

  [derivation]: #gloss-derivation

- [store derivation]{#gloss-store-derivation}

  A [derivation] represented as a [store object].

  See [Store Derivation](@docroot@/store/derivation/index.md#store-derivation) for details.

  [store derivation]: #gloss-store-derivation

- [directed acyclic graph]{#gloss-directed-acyclic-graph}

  A [directed acyclic graph](https://en.wikipedia.org/wiki/Directed_acyclic_graph) (DAG) is graph whose edges are given a direction ("a to b" is not the same edge as "b to a"), and for which no possible path (created by joining together edges) forms a cycle.

  DAGs are very important to Nix.
  In particular, the non-self-[references][reference] of [store object][store object] form a cycle.

- [derivation path]{#gloss-derivation-path}

  A [store path] which uniquely identifies a [store derivation].

  See [Referencing Store Derivations](@docroot@/store/derivation/index.md#derivation-path) for details.

  Not to be confused with [deriving path].

  [derivation path]: #gloss-derivation-path

- [derivation expression]{#gloss-derivation-expression}

  A description of a [store derivation] using the [`derivation` primitive](./language/derivations.md) in the [Nix language].

  [derivation expression]: #gloss-derivation-expression

- [instantiate]{#gloss-instantiate}, instantiation

  Translate a [derivation expression] into a [store derivation].

  See [`nix-instantiate`](./command-ref/nix-instantiate.md), which produces a store derivation from a Nix expression that evaluates to a derivation.

  [instantiate]: #gloss-instantiate

- [realise]{#gloss-realise}, realisation

  Ensure a [store path] is [valid][validity].

  This can be achieved by:
  - Fetching a pre-built [store object] from a [substituter]
  - [Building](@docroot@/store/building.md) the corresponding [store derivation]
  - Delegating to a [remote machine](@docroot@/command-ref/conf-file.md#conf-builders) and retrieving the outputs

  See [`nix-store --realise`](@docroot@/command-ref/nix-store/realise.md) for a detailed description of the algorithm.

  See also [`nix-build`](./command-ref/nix-build.md) and [`nix build`](./command-ref/new-cli/nix3-build.md) (experimental).

  [realise]: #gloss-realise

- [content-addressing derivation]{#gloss-content-addressing-derivation}

  A derivation which has the
  [`__contentAddressed`](./language/advanced-attributes.md#adv-attr-__contentAddressed)
  attribute set to `true`.

- [fixed-output derivation]{#gloss-fixed-output-derivation} (FOD)

  A [store derivation] where a cryptographic hash of the [output] is determined in advance using the [`outputHash`](./language/advanced-attributes.md#adv-attr-outputHash) attribute, and where the [`builder`](@docroot@/language/derivations.md#attr-builder) executable has access to the network.

- [store]{#gloss-store}

  A collection of [store objects][store object], with operations to manipulate that collection.
  See [Nix Store](./store/index.md) for details.

  There are many types of stores, see [Store Types](./store/types/index.md) for details.

  [store]: #gloss-store

- [Nix instance]{#gloss-nix-instance}
  <!-- ambiguous -->
  1. An installation of Nix, which includes the presence of a [store], and the Nix package manager which operates on that store.
     A local Nix installation and a [remote builder](@docroot@/advanced-topics/distributed-builds.md) are two examples of Nix instances.
  2. A running Nix process, such as the `nix` command.

- [binary cache]{#gloss-binary-cache}

  A *binary cache* is a Nix store which uses a different format: its
  metadata and signatures are kept in `.narinfo` files rather than in a
  [Nix database]. This different format simplifies serving store objects
  over the network, but cannot host builds. Examples of binary caches
  include S3 buckets and the [NixOS binary cache](https://cache.nixos.org).

- [store path]{#gloss-store-path}

  The location of a [store object] in the file system, i.e., an immediate child of the Nix store directory.

  > **Example**
  >
  > `/nix/store/a040m110amc4h71lds2jmr8qrkj2jhxd-git-2.38.1`

  See [Store Path](@docroot@/store/store-path.md) for details.

  [store path]: #gloss-store-path

- [file system object]{#gloss-file-system-object}

  The Nix data model for representing simplified file system data.

  See [File System Object](@docroot@/store/file-system-object.md) for details.

  [file system object]: #gloss-file-system-object

- [store object]{#gloss-store-object}

  Part of the contents of a [store].

  A store object consists of a [file system object], [references][reference] to other store objects, and other metadata.
  It can be referred to by a [store path].

  See [Store Object](@docroot@/store/store-object.md) for details.

  [store object]: #gloss-store-object

- [IFD]{#gloss-ifd}

  [Import From Derivation](./language/import-from-derivation.md)

- [input-addressed store object]{#gloss-input-addressed-store-object}

  A store object produced by building a
  non-[content-addressed](#gloss-content-addressing-derivation),
  non-[fixed-output](#gloss-fixed-output-derivation)
  derivation.

  See [input-addressing derivation outputs](store/derivation/outputs/input-address.md) for details.

- [content-addressed store object]{#gloss-content-addressed-store-object}

  A [store object] which is [content-addressed](#gloss-content-address),
  i.e. whose [store path] is determined by its contents.
  This includes derivations, the outputs of [content-addressing derivations](#gloss-content-addressing-derivation), and the outputs of [fixed-output derivations](#gloss-fixed-output-derivation).

  See [Content-Addressing Store Objects](@docroot@/store/store-object/content-address.md) for details.

- [substitute]{#gloss-substitute}

  A substitute is a command invocation stored in the [Nix database] that
  describes how to build a store object, bypassing the normal build
  mechanism (i.e., derivations). Typically, the substitute builds the
  store object by downloading a pre-built version of the store object
  from some server.

- [substituter]{#gloss-substituter}

  An additional [store]{#gloss-store} from which Nix can obtain store objects instead of building them.
  Often the substituter is a [binary cache](#gloss-binary-cache), but any store can serve as substituter.

  See the [`substituters` configuration option](./command-ref/conf-file.md#conf-substituters) for details.

  [substituter]: #gloss-substituter

- [purity]{#gloss-purity}

  The assumption that equal Nix derivations when run always produce
  the same output. This cannot be guaranteed in general (e.g., a
  builder can rely on external inputs such as the network or the
  system time) but the Nix model assumes it.

- [impure derivation]{#gloss-impure-derivation}

  [An experimental feature](@docroot@/development/experimental-features.md#xp-feature-impure-derivations) that allows derivations to be explicitly marked as impure,
  so that they are always rebuilt, and their outputs not reused by subsequent calls to realise them.

- [Nix database]{#gloss-nix-database}

  An SQlite database to track [reference]s between [store object]s.
  This is an implementation detail of the [local store].

  Default location: `/nix/var/nix/db`.

  [Nix database]: #gloss-nix-database

- [Nix expression]{#gloss-nix-expression}

  A syntactically valid use of the [Nix language].

  > **Example**
  >
  > The contents of a `.nix` file form a Nix expression.

  Nix expressions specify [derivation expressions][derivation expression], which are [instantiated][instantiate] into the Nix store as [store derivations][store derivation].
  These derivations can then be [realised][realise] to produce [outputs][output].

  > **Example**
  >
  > Building and deploying software using Nix entails writing Nix expressions to describe [packages][package] and compositions thereof.

- [reference]{#gloss-reference}

  An edge from one [store object] to another.

  See [References](@docroot@/store/store-object.md#references) for details.

  [reference]: #gloss-reference

  See [References](@docroot@/store/store-object.md#references) for details.

- [reachable]{#gloss-reachable}

  A store path `Q` is reachable from another store path `P` if `Q`
  is in the *closure* of the *references* relation.

  See [References](@docroot@/store/store-object.md#references) for details.

- [closure]{#gloss-closure}

  The closure of a store path is the set of store paths that are
  directly or indirectly “reachable” from that store path; that is,
  it’s the closure of the path under the *references* relation. For
  a package, the closure of its derivation is equivalent to the
  build-time dependencies, while the closure of its [output path] is
  equivalent to its runtime dependencies. For correct deployment it
  is necessary to deploy whole closures, since otherwise at runtime
  files could be missing. The command `nix-store --query --requisites ` prints out
  closures of store paths.

  As an example, if the [store object] at path `P` contains a [reference]
  to a store object at path `Q`, then `Q` is in the closure of `P`. Further, if `Q`
  references `R` then `R` is also in the closure of `P`.

  See [References](@docroot@/store/store-object.md#references) for details.

  [closure]: #gloss-closure

- [requisite]{#gloss-requisite}

  A store object [reachable] by a path (chain of references) from a given [store object].
  The [closure] is the set of requisites.

  See [References](@docroot@/store/store-object.md#references) for details.

- [referrer]{#gloss-referrer}

  A reversed edge from one [store object] to another.

- [output]{#gloss-output}

  A [store object] produced by a [store derivation].
  See [the `outputs` argument to the `derivation` function](@docroot@/language/derivations.md#attr-outputs) for details.

  [output]: #gloss-output

- [output path]{#gloss-output-path}

  The [store path] to the [output] of a [store derivation].

  [output path]: #gloss-output-path

- [output closure]{#gloss-output-closure}\
  The [closure] of an [output path]. It only contains what is [reachable] from the output.

- [deriving path]{#gloss-deriving-path}

  Deriving paths are a way to refer to [store objects][store object] that might not yet be [realised][realise].

  See [Deriving Path](./store/derivation/index.md#deriving-path) for details.

  Not to be confused with [derivation path].

- [deriver]{#gloss-deriver}

  The [store derivation] that produced an [output path].

  The deriver for an output path can be queried with the `--deriver` option to
  [`nix-store --query`](@docroot@/command-ref/nix-store/query.md).

- [validity]{#gloss-validity}

  A store path is valid if all [store object]s in its [closure] can be read from the [store].

  For a [local store], this means:
  - The store path leads to an existing [store object] in that [store].
  - The store path is listed in the [Nix database] as being valid.
  - All paths in the store path's [closure] are valid.

  [validity]: #gloss-validity
  [local store]: @docroot@/store/types/local-store.md

- [user environment]{#gloss-user-env}

  An automatically generated store object that consists of a set of
  symlinks to “active” applications, i.e., other store paths. These
  are generated automatically by
  [`nix-env`](./command-ref/nix-env.md). See *profiles*.

- [profile]{#gloss-profile}

  A symlink to the current *user environment* of a user, e.g.,
  `/nix/var/nix/profiles/default`.

- [installable]{#gloss-installable}

  Something that can be realised in the Nix store.

  See [installables](./command-ref/new-cli/nix.md#installables) for [`nix` commands](./command-ref/new-cli/nix.md) (experimental) for details.

- [Nix Archive (NAR)]{#gloss-nar}

  A *N*ix *AR*chive. This is a serialisation of a path in the Nix
  store. It can contain regular files, directories and symbolic
  links.  NARs are generated and unpacked using `nix-store --dump`
  and `nix-store --restore`.

  See [Nix Archive](store/file-system-object/content-address.html#serial-nix-archive) for details.

- [`∅`]{#gloss-empty-set}

  The empty set symbol. In the context of profile history, this denotes a package is not present in a particular version of the profile.

- [`ε`]{#gloss-epsilon}

  The epsilon symbol. In the context of a package, this means the version is empty. More precisely, the derivation does not have a version attribute.

- [package]{#package}

  A software package; files that belong together for a particular purpose, and metadata.

  Nix represents files as [file system objects][file system object], and how they belong together is encoded as [references][reference] between [store objects][store object] that contain these file system objects.

  The [Nix language] allows denoting packages in terms of [attribute sets](@docroot@/language/types.md#type-attrs) containing:
  - attributes that refer to the files of a package, typically in the form of [derivation outputs](#gloss-output),
  - attributes with metadata, such as information about how the package is supposed to be used.

  The exact shape of these attribute sets is up to convention.

  [package]: #package

- [string interpolation]{#gloss-string-interpolation}

  Expanding expressions enclosed in `${ }` within a [string], [path], or [attribute name].

  See [String interpolation](./language/string-interpolation.md) for details.

  [string]: ./language/types.md#type-string
  [path]: ./language/types.md#type-path
  [attribute name]: ./language/types.md#type-attrs

- [base directory]{#gloss-base-directory}

  The location from which relative paths are resolved.

  - For expressions in a file, the base directory is the directory containing that file.
    This is analogous to the directory of a [base URL](https://datatracker.ietf.org/doc/html/rfc1808#section-3.3).
    <!-- which is sufficient for resolving non-empty URLs -->

  <!--
    The wording here may look awkward, but it's for these reasons:
      * "with --expr": it's a flag, and not an option with an accompanying value
      * "written in": the expression itself must be written as an argument,
        whereas the more natural "passed as an argument" allows an interpretation
        where the expression could be passed by file name.
    -->
  - For expressions written in command line arguments with [`--expr`](@docroot@/command-ref/opt-common.html#opt-expr), the base directory is the current working directory.

  [base directory]: #gloss-base-directory

- [experimental feature]{#gloss-experimental-feature}

  Not yet stabilized functionality guarded by named experimental feature flags.
  These flags are enabled or disabled with the [`experimental-features`](./command-ref/conf-file.html#conf-experimental-features) setting.

  See the contribution guide on the [purpose and lifecycle of experimental feaures](@docroot@/development/experimental-features.md).


[Nix language]: ./language/index.md
