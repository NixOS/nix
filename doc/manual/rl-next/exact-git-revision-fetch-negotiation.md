---
synopsis: Exact Git revision fetches reuse cached history
---

Repeated Git fetches of related exact revisions now negotiate from the previously fetched revision, avoiding redundant object transfers.

This requires Git 2.19 or newer.
