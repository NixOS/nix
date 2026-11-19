# Lazy paths

Primary goal: *Do not over-fetch sources*

In other words, ingest local source trees without any unnecessary reads, and
therefore avoid hashing `fetchTree` outputs and flake inputs, using path values
that do not share a root with `/.`.

In progress. See [lazy paths tracking].

## Motivation

Nix has a requirement of hermeticity: the ability to know and collect
all inputs to the evaluation and the build. To make this practical,
Nix implements *fetching* in both those stages.

Fetching in turn must be reproducible, and this extends to the
development workflow: local invocations should produce the same
results as if they were pushed and fetched. For this reason, Nix
flakes "fetch" their local sources, but this came at the cost of
performance.

Local fetching has been optimised significantly between Nix
2.18 and 2.35. Notably, Sergei has made the copying to the store optional,
in what he also called lazy paths.
Unfortunately that solution is still bound by the language semantics,
requiring the entire tree to be read and hashed, in order to produce the
correct store path.


Besides the performance improvement, I would argue these benefits:
- Improved UX: mounting sources into the "host file system" makes little to no
  sense conceptually, and indeed it gives rise to strange error scenarios
  because symlinks and other upward references can hardly be interpreted as
  direct mistakes, delaying the abort, which will be for a different and strange
  reason.
- Support for more refined caching strategies. A tracing cache only needs to
  validate what was actually read, but by ingesting a whole-tree hash, its
  hit rate degrades to exactly the kind that can be achieved with the current
  flakes-based coarse eval cache, adding only impure mode support.

## Alternatives considered

### `git-hashing`

The overfetching problem could be *alleviated* by `git-hashing` in limited scenarios:
- fetch by commit hash or integration with any git-based file systems, or
- usage with FUSE file systems and service that can cheaply represent
  uncommitted states using a tree hash

However, besides these limitations, the tree that is available in these 
scenarios may not represent Nix's view of those sources, when it comes to
submodules, Git LFS, and any other transformations.

These transformations essentially give rise a whole family of hashing schemes
that resemble or build on `git-hashing`.
Whether such complexity belongs in the hashing layer seems debatable.

Nonetheless, it's promising feature in its own right, but it is not a solution
to the problem of having to ingest the whole git workdir in practice for
most users.

### Thunked rope representation for strings

An earlier candidate considered was a lazy "rope" representation for strings.
These strings would be built up from concatenated segments and materialised
into their byte form when demanded (e.g. hashing, equality, propagation into a
derivation input).

This was not the first approach attempted, as strings sit on a more
performance-sensitive path through the evaluator than path values do, and
strings would perhaps be easier to use in ways that force the byte
materialisation anyway, therefore causing the hashes to be computed regardless.

Furthermore, we deal with a language-design constraint: thunk presence must
never be language-observable, so this solution, at least by itself, would leave
no option for user expressions to handle lazy-fetched sources with extra care,
where that makes sense (e.g. in libraries).

The Module System might have been a reason to consider ropes, but some digging
revealed the feared module-path deduplication to be handled entirely within
`builtins.genericClosure`, which, as an interface, does not impose anything
on the comparators used inside.

Two changes to `genericClosure` were sufficient to make the module system work
correctly without overfetching: a custom comparator and a flag to enable paths
to compare equal to their `toString` representation.

## Design choices

### Paths and source roots: expressing intent

A path value names a position in an abstract source tree.
Concretely: an accessor that knows how to perform input operations, plus a path
that points into this accessor.

This is somewhat similar to a store path, but a store path uses a content
address instead of an accessor: `"${storeDir}/${hash}-source"` for typical
fetched sources.

A major difference is that the store path is effectively mounted into the
system's filesystem, and as a consequence, its meaning has shifted from
an abstract, relocatable source tree to something more specific.

Keeping the value abstract lets equality, comparison, and
structural operations like `dirOf`/`baseNameOf` decide without
materialising the tree. Placement into the store happens only when
something genuinely needs the placed form.

The abstract answers also behave more predictably. Historically, a `flake.nix`
at its source root performing `baseNameOf ./.` returned `<hash>-source`, which 
is an artefact of the storepath the tree was rendered into, and almost never
what the caller intended.

Under the abstract form the root has no basename, and `baseNameOf` reports that
directly.
When the store-path semantics are desired for a source root, they are a mere
string interpolation away.

The abstract, intent-carrying form also lets the runtime detect boundary
escapes: a `..` segment that pops past the accessor's root, or a symlink whose
target leaves the source tree.
These report explicit errors at the call site rather than being silently
followed.

Path values also keep untracked-file reporting simple: the accessor
identifies which source tree a path lives in directly, with no
indirection through a stringified form to navigate. Fixable in other ways,
but simple is good.

The alternative was not picked: reporting the basename of the root as
`"<hash>-source"` could make a few extra aexpressions "just work" even when
imported through a lazy path, but it increases the risk of overfetching.

Similarly, `dirOf src` does not flip to `${storeDir}`.

### Kinds of `SourceRoot`

Broadly, a path's source root can be one of three kinds of places:
- fetched tree
- a real filesystem location
- nix-internal scaffolding

Existing Nix expressions expect `toString` of a path value to be a unique,
deterministic identifier, and some expressions even expect to read from it.
(Whether that was the original intent is questionable: paths lack
string context, and path values' interpolation already emphasises
copying. Debatable, but questions don't change two decades of expressions.)

The right `toString` behaviour differs in each.

Note that those three distinctions are language-level ones, that do not belong
in the `SourceAccessor` interface, so to support each origin without forcing
one shape on all paths, we introduce `SourceRoot` to carry a *kind* tag having
on of three values:

- **System** — a real filesystem location. `toString` returns the
  absolute path; `${...}` copies the subpath into the store. Models
  `/etc/foo`, `/nix/store/X-source`, and other literal-rooted paths.
- **Copyable** — a fetched tree, or an in-language tree built via
  `builtins.makePath`. Both `toString` and `${...}` materialise the
  root once and render as `<storePath>/<subpath>`.
  This name is up for bikeshedding.
- **Internal** — nix-internal scaffolding (corepkgs, derivation, helpers).
  `toString` throws; positions resolve to `null`; copies are rejected.
  This kind exists so that leaking an internal value into language space fails
  loudly rather than leaking data bound to the evaluating Nix version into an
  otherwise hermetic evaluation.

The tag lives on the SourceRoot rather than on individual path
values. All paths under one accessor share its semantics, so the
kind decision is paid once at admission rather than re-derived per
operation.

### Flakes opt-in

A flake's `outPath` stays a string. It has to name both the root of
the source tree and the position inside it, which a store path does
and a path value does not: `"${flakePath + "/relative"}"` names a copy of
the subdirectory and loses the root, which breaks the import of a
sibling file like `../relative2/default.nix`.

Since the kind of `./.`, matters, including for filesets, which needs a simple patch.
This means flipping the kind by default breaks a portion of current flakes,
we choose to make loading a flake by lazy path opt-in.

Whether to copy the flake source to the store is elected per flake, by four
declarations:

- `inputs.self.copyToStore`: whether `./.` is a store path or a lazy path in this flake.
- `inputs.<name>.copyToStore`: whether this input loads and/or presents as a store path or lazy path.
- a marker file at the flake root, `/.nix-flake-lazy-paths-supported`, equivalent to `inputs.self.copyToStore = false;` for all flakes inside.
- `flake-default-copy-to-store`, option and/or ambient setting.

## Feature flag

Significant development risk has been reduced; see [lazy paths tracking].
This work inherently requires the `fetch-tree` or `flakes` experiments to be
opted in to, so the value of a new flag would be limited.
Since learnings about lazy paths inform the risk profile of those experiments,
adding more obstructions seems counterproductive.

[lazy paths tracking]: https://github.com/NixOS/nix/issues/16720
