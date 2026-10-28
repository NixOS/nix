---
synopsis: "`nix log` only keeps the build's colors on a terminal"
issues: [14700]
prs: [16714]
---

`nix log` now removes ANSI escapes from the stored log when stdout is not a terminal, or when `NO_COLOR` is set.
The rest of the log, including tabs, is unchanged.
Set `CLICOLOR_FORCE` to keep them.
