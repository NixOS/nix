---
synopsis: "`nix repl` now respects Ctrl-C while printing values"
prs: 9927
---

`nix repl` will now halt immediately when Ctrl-C is pressed while it's printing
a value. This is useful if you got curious about what would happen if you
printed all of Nixpkgs.
