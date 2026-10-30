---
synopsis: "Styled output on stdout is plain when stdout is not a terminal"
issues: [16710, 9486, 4626]
prs: [16711]
---

`nix profile list`, `nix profile history`, `nix flake metadata`, `nix flake show`, `nix search` and `nix store diff-closures` no longer write ANSI bold and color to stdout when it is not a terminal, or when `NO_COLOR` is set.
Set `CLICOLOR_FORCE` to keep the styling.
