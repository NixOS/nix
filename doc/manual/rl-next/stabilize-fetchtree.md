---
synopsis: Stabilize fetchTree
prs: 10068
---

The core of `fetchTree` is now stable.
This includes
- the `fetchTree` function itself
- all the existing fetchers, except `git` (still unstable because of some reproducibility concerns)
