---
synopsis: "Eval cache: fix cache regressions"
prs: 11086
issues: 10570
---

This update addresses two bugs in the evaluation cache system:

1. Regression in #10570: The evaluation cache was not being persisted in `nix develop` because `evalCaches` retained references to the caches and was never freed.
2. Nix could sometimes try to commit the evaluation cache SQLite transaction without there being an active transaction, resulting in non-error errors being printed.

These bug fixes ensure that the evaluation cache is correctly managed and errors are appropriately handled.

Author: [**Lexi Mattick (@kognise)**](https://github.com/kognise)
