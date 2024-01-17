---
synopsis: "Nix now uses `libgit2` for Git fetching"
prs:
  - 9240
  - 9241
  - 9258
  - 9480
issues:
  - 5313
---

Nix has built-in support for fetching sources from Git, during evaluation and locking; outside the sandbox.
The existing implementation based on the Git CLI had issues regarding reproducibility and performance.

Most of the original `fetchGit` behavior has been implemented using the `libgit2` library, which gives the fetcher fine-grained control.

Known issues:
- The `export-subst` behavior has not been reimplemented. [Partial](https://github.com/NixOS/nix/pull/9391#issuecomment-1872503447) support for this Git feature is feasible, but it did not make the release window.
