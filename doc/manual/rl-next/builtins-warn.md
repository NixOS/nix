---
synopsis: "New builtin: `builtins.warn`"
issues: 306026
prs: 10592
---

`builtins.warn` behaves like `builtins.trace "warning: ${msg}"`, has an accurate log level, and is controlled by the options
[`debugger-on-trace`](@docroot@/command-ref/conf-file.md#conf-debugger-on-trace),
[`debugger-on-warn`](@docroot@/command-ref/conf-file.md#conf-debugger-on-warn) and
[`abort-on-warn`](@docroot@/command-ref/conf-file.md#conf-abort-on-warn).
