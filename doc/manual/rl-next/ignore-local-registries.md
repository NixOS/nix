---
synopsis: "Flake lock file generation now ignores local registries"
prs: [12019]
---

When resolving indirect flake references like `nixpkgs` in `flake.nix` files, Nix will no longer use the system and user flake registries. It will only use the global flake registry and overrides given on the command line via `--override-flake`.

This avoids accidents where users have local registry overrides that map `nixpkgs` to a `path:` flake in the local file system, which then end up in committed lock files pushed to other users.

In the future, we may remove the use of the registry during lock file generation altogether. It's better to explicitly specify the URL of a flake input. For example, instead of
```nix
{
  outputs = { self, nixpkgs }: { ... };
}
```
write
```nix
{
  inputs.nixpkgs.url = "github:NixOS/nixpkgs/nixos-24.11";
  outputs = { self, nixpkgs }: { ... };
}
```
