---
synopsis: Removing and upgrading packages using flake URLs
prs: 10198
---

It is now possible to remove and upgrade packages in `nix profile` using flake URLs.

```console
$ nix profile install nixpkgs#firefox
$ nix profile upgrade nixpkgs#firefox
$ nix profile remove nixpkgs#firefox
```
