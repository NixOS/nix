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

    See [`nix show-derivation`](./command-ref/new-cli/nix3-show-derivation.md) (experimental) for displaying the contents of store derivations.

    [store derivation]: #gloss-store-derivation

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

    + A *local  store* exists  on the filesystem of
      the machine where Nix is  invoked. You can use other
      local stores  by passing  the `--store` flag  to the
      `nix` command.  Local stores can be used for building derivations.

    + A  *remote store*  exists  anywhere  other than  the
      local  filesystem. One  example is  the `/nix/store`
      directory on another machine,  accessed via `ssh` or
      served by the `nix-serve` Perl script.

    [store]: #gloss-store

  - [chroot store]{#gloss-chroot-store}\
    A local store whose canonical path is anything other than `/nix/store`.

  - [binary cache]{#gloss-binary-cache}\
    A *binary cache* is a Nix store which uses a different format: its
    metadata and signatures are kept in `.narinfo` files rather than in a
    Nix database.  This different format simplifies serving store objects
    over the network, but cannot host builds.  Examples of binary caches
    include S3 buckets and the [NixOS binary
    cache](https://cache.nixos.org).

  - [store path]{#gloss-store-path}\
    The location of a [store object] in the file system, i.e., an
    immediate child of the Nix store directory.

    Example: `/nix/store/a040m110amc4h71lds2jmr8qrkj2jhxd-git-2.38.1`

    [store path]: #gloss-store-path

  - [store object]{#gloss-store-object}\
    A file that is an immediate child of the Nix store directory. These
    can be regular files, but also entire directory trees. Store objects
    can be sources (objects copied from outside of the store),
    derivation outputs (objects produced by running a build task), or
    derivations (files describing a build task).

    [store object]: #gloss-store-object

  - [input-addressed store object]{#gloss-input-addressed-store-object}\
    A store object produced by building a
    non-[content-addressed](#gloss-content-addressed-derivation),
    non-[fixed-output](#gloss-fixed-output-derivation)
    derivation.

  - [output-addressed store object]{#gloss-output-addressed-store-object}\
    A store object whose store path hashes its content.  This
    includes derivations, the outputs of
    [content-addressed derivations](#gloss-content-addressed-derivation),
    and the outputs of
    [fixed-output derivations](#gloss-fixed-output-derivation).

  - [substitute]{#gloss-substitute}\
    A substitute is a command invocation stored in the Nix database that
    describes how to build a store object, bypassing the normal build
    mechanism (i.e., derivations). Typically, the substitute builds the
    store object by downloading a pre-built version of the store object
    from some server.

  - [substituter]{#gloss-substituter}\
    A *substituter* is an additional store from which Nix will
    copy store objects it doesn't have.  For details, see the
    [`substituters` option](./command-ref/conf-file.md#conf-substituters).

  - [purity]{#gloss-purity}\
    The assumption that equal Nix derivations when run always produce
    the same output. This cannot be guaranteed in general (e.g., a
    builder can rely on external inputs such as the network or the
    system time) but the Nix model assumes it.

  - [Nix expression]{#gloss-nix-expression}\
    A high-level description of software packages and compositions
    thereof. Deploying software using Nix entails writing Nix
    expressions for your packages. Nix expressions are translated to
    derivations that are stored in the Nix store. These derivations can
    then be built.

  - [reference]{#gloss-reference}\
    A store path `P` is said to have a reference to a store path `Q` if
    the store object at `P` contains the path `Q` somewhere. The
    *references* of a store path are the set of store paths to which it
    has a reference.

    A derivation can reference other derivations and sources (but not
    output paths), whereas an output path only references other output
    paths.

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
    files could be missing. The command `nix-store -qR` prints out
    closures of store paths.

    As an example, if the store object at path `P` contains a reference
    to path `Q`, then `Q` is in the closure of `P`. Further, if `Q`
    references `R` then `R` is also in the closure of `P`.

  - [output path]{#gloss-output-path}\
    A [store path] produced by a [derivation].

    [output path]: #gloss-output-path

  - [deriver]{#gloss-deriver}\
    The deriver of an *output path* is the store
    derivation that built it.

  - [validity]{#gloss-validity}\
    A store path is considered *valid* if it exists in the file system,
    is listed in the Nix database as being valid, and if all paths in
    its closure are also valid.

  - [user environment]{#gloss-user-env}\
    An automatically generated store object that consists of a set of
    symlinks to “active” applications, i.e., other store paths. These
    are generated automatically by
    [`nix-env`](./command-ref/nix-env.md). See *profiles*.

  - [profile]{#gloss-profile}\
    A symlink to the current *user environment* of a user, e.g.,
    `/nix/var/nix/profiles/default`.

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
