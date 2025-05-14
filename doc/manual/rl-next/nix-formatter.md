---
synopsis: "Add `nix formatter build` and `nix formatter run` commands"
issues:
prs: [13063]
---

`nix formatter run` is an alias for `nix fmt`. Nothing new there.

`nix formatter build` is sort of like `nix build`: it builds, links, and prints a path to the formatter program:

```
$ nix formatter build
/nix/store/cb9w44vkhk2x4adfxwgdkkf5gjmm856j-treefmt/bin/treefmt
```

Note that unlike `nix build`, this prints the full path to the program, not just the store path (in the example above that would be `/nix/store/cb9w44vkhk2x4adfxwgdkkf5gjmm856j-treefmt`).
