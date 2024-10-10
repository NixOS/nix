---
synopsis: Flakes are no longer substituted
prs: [10612]
---

Nix will no longer attempt to substitute the source code of flakes from a binary cache. This functionality was broken because it could lead to different evaluation results depending on whether the flake was available in the binary cache, or even depending on whether the flake was already in the local store.

Author: [**@edolstra**](https://github.com/edolstra)
