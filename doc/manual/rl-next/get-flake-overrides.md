---
synopsis: "`builtins.getFlake` allows inputs to be overriden"
prs: [11952]
---

`builtins.getFlake` now allows you to override the inputs of a flake, using the same override mechanism used for flake inputs in `flake.nix`. For example, to fetch and call the `NixOS/nix` flake and override its `nixpkgs` input:

```nix
builtins.getFlake {
  url = "github:NixOS/nix/55bc52401966fbffa525c574c14f67b00bc4fb3a";
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/c69a9bffbecde46b4b939465422ddc59493d3e4d";
}
```
