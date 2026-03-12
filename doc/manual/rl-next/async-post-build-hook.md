---
synopsis: Make post-build-hook asynchronous
prs: [15451]
issues: [15406]
---

This change makes the `post-build-hook` run asynchronously but still as part of the goal.
This retains the current behavior that a waiting goal will not start until the `post-build-hook` of the goal it is waiting on completes.
However, multiple `post-build-hook`s can now run concurrently just as multiple goals can run concurrently.
