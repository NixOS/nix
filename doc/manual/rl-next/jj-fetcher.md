---
synopsis: Flake support for Jujutsu (jj) working copies
issues: [15651]
---

Nix now understands [Jujutsu](https://jj-vcs.github.io/) working copies.
Previously, evaluating a flake in a jj working copy with no `.git` directory
(most notably a `jj workspace add` workspace) fell back to the `path` fetcher,
copying the entire working directory — including build artifacts and untracked
files — into the store with no filtering.

A new `jj` input scheme detects `.jj` directories and shells out to the `jj` CLI
to determine which files are tracked, so only those are copied:

```nix
builtins.fetchTree { type = "jj"; url = "file:///path/to/working-copy"; }
```

A flake reference to a local path that contains a `.jj` directory is routed to
this fetcher automatically, in preference to the Git fetcher: a colocated
repository (`jj git init --colocate`) has both `.git` and `.jj`, and the presence
of `.jj` is taken as a deliberate choice to use jj. (The jj fetcher does not yet
support the Git fetcher's `submodules`, `lfs` or signed-commit verification; a
colocated repository that needs those can be selected explicitly with a
`git+file://` URL.)

An explicit revision or bookmark can also be fetched:

```nix
builtins.fetchTree { type = "jj"; url = "file:///path/to/repo"; rev = "<commit-id>"; }
builtins.fetchTree { type = "jj"; url = "file:///path/to/repo"; ref = "<bookmark>"; }
```

Remote repositories are supported too. jj's only remote-capable backend is Git,
so a remote is cloned with `jj git clone` via a `jj+git+<transport>` scheme
(`jj+git+https`, `jj+git+ssh`, `jj+git+http`, `jj+git+file`), with optional
shallow fetching:

```nix
builtins.fetchTree { type = "jj"; url = "git+https://example.org/repo"; }
builtins.fetchTree { type = "jj"; url = "git+ssh://example.org/repo"; ref = "main"; shallow = true; }
```
