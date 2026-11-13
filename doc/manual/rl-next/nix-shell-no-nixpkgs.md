---
synopsis: "`nix-shell` no longer prints an error when `<nixpkgs>` is not in the search path"
prs: [16584]
---

`nix-shell` looks for `bashInteractive` in `<nixpkgs>` to use as the interactive shell.
On installs without channels (the default of `nix-installer`), `<nixpkgs>` is not in the search path; `nix-shell` then used the fallback shell but first printed an error that looked like a failure.
It now prints a single short note instead.
Other errors while evaluating `bashInteractive` are still shown in full.
