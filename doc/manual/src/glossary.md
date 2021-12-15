# Glossary

  - derivation\
    A description of a build action. The result of a derivation is a
    store object. Derivations are typically specified in Nix expressions
    using the [`derivation` primitive](expressions/derivations.md). These are
    translated into low-level *store derivations* (implicitly by
    `nix-env` and `nix-build`, or explicitly by `nix-instantiate`).

  - store\
    The location in the file system where store objects live. Typically
    `/nix/store`.

  - store path\
    The location in the file system of a store object, i.e., an
    immediate child of the Nix store directory.

  - store object\
    A file that is an immediate child of the Nix store directory. These
    can be regular files, but also entire directory trees. Store objects
    can be sources (objects copied from outside of the store),
    derivation outputs (objects produced by running a build action), or
    derivations (files describing a build action).

  - substitute\
    A substitute is a command invocation stored in the Nix database that
    describes how to build a store object, bypassing the normal build
    mechanism (i.e., derivations). Typically, the substitute builds the
    store object by downloading a pre-built version of the store object
    from some server.

  - purity\
    The assumption that equal Nix derivations when run always produce
    the same output. This cannot be guaranteed in general (e.g., a
    builder can rely on external inputs such as the network or the
    system time) but the Nix model assumes it.

  - Nix expression\
    A high-level description of software packages and compositions
    thereof. Deploying software using Nix entails writing Nix
    expressions for your packages. Nix expressions are translated to
    derivations that are stored in the Nix store. These derivations can
    then be built.

  - reference\
    A store path `P` is said to have a reference to a store path `Q` if
    the store object at `P` contains the path `Q` somewhere. The
    *references* of a store path are the set of store paths to which it
    has a reference.

    A derivation can reference other derivations and sources (but not
    output paths), whereas an output path only references other output
    paths.

  - reachable\
    A store path `Q` is reachable from another store path `P` if `Q`
    is in the *closure* of the *references* relation.

  - closure\
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

  - output path\
    A store path produced by a derivation.

  - deriver\
    The deriver of an *output path* is the store
    derivation that built it.

  - validity\
    A store path is considered *valid* if it exists in the file system,
    is listed in the Nix database as being valid, and if all paths in
    its closure are also valid.

  - user environment\
    An automatically generated store object that consists of a set of
    symlinks to “active” applications, i.e., other store paths. These
    are generated automatically by
    [`nix-env`](command-ref/nix-env.md). See *profiles*.

  - profile\
    A symlink to the current *user environment* of a user, e.g.,
    `/nix/var/nix/profiles/default`.

  - NAR\
    A *N*ix *AR*chive. This is a serialisation of a path in the Nix
    store. It can contain regular files, directories and symbolic
    links.  NARs are generated and unpacked using `nix-store --dump`
    and `nix-store --restore`.
  - `∅` \
    The empty set symbol. In the context of profile history, this denotes a package is not present in a particular version of the profile.
  - `ε` \
    The epsilon symbol. In the context of a package, this means the version is empty. More precisely, the derivation does not have a version attribute.
