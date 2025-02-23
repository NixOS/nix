---
synopsis: "Add pure-storepath-builtin experimental feature"
issues: [ 5868 ]
prs: [ 12141 ]
---

The `pure-storepath-builtin` [experimental feature](@docroot@/language/builtins.md) was added, which controls whether [`storePath`](@docroot@/language/builtins.md#builtins-storePath) should be allowed in pure evaluation mode. If the `pure-storepath-builtin`
experimental feature is enabled, `storePath` will no longer error when run in pure evaluation mode.

This is intended as a stop-gap solution while the correct behaviour of `storePath` is debated in the linked issue.
