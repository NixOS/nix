---
synopsis: "REPL, debugger, help and why-depends output is plain when it is not going to a terminal"
issues: [16824, 16825, 16826]
prs: [16827]
---

`nix repl` no longer writes ANSI escape codes to stdout when it is not a terminal, or when `NO_COLOR` is set: printed values, `:doc`, and the debugger's `:bt`, `:st` and `:env` colors.
`nix why-depends` follows the same rule.
Set `CLICOLOR_FORCE` to keep the styling.
`nix help` also decides by stdout now, before the pager takes it over.
The message printed when Nix terminates on an internal error is only colored when stderr is a terminal.
