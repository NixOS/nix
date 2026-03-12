---
synopsis: Add async-post-build-hook option
prs: [15451]
issues: [15406]
---

An `async-post-build-hook` option is added that allows running post build hooks asynchronously.
The number of parallel async-post-build-hooks that can be run at a time can be controlled by a `max-async-post-build-hook-jobs` setting.
