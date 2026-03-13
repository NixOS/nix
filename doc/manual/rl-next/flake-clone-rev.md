---
synopsis: "`nix flake clone` now supports cloning a specific revision"
prs: []
issues: [15385]
---

`nix flake clone` now supports the `rev` query parameter. Previously, specifying a revision would result in an "unimplemented" error. Now, the repository is cloned and then checked out at the specified revision.

```console
$ nix flake clone --dest ./repo 'git+file:///path/to/repo?ref=main&rev=abc123...'
```
