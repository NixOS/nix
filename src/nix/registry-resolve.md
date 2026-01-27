R""(

# Examples

* Resolve the `nixpkgs` and `blender-bin` flakerefs:

  ```console
  # nix registry resolve nixpkgs blender-bin
  github:NixOS/nixpkgs/nixpkgs-unstable
  github:edolstra/nix-warez?dir=blender
  ```

* Resolve an indirect flakeref with a branch override:

  ```console
  # nix registry resolve nixpkgs/25.05
  github:NixOS/nixpkgs/25.05
  ```

# Description

This command resolves indirect flakerefs (e.g. `nixpkgs`) to direct flakerefs (e.g. `github:NixOS/nixpkgs`) using the flake registries. It looks up each provided flakeref in all available registries (flag, user, system, and global) and returns the resolved direct flakeref on a separate line on standard output. It does not fetch any flakes.

The resolution process may apply multiple redirections if necessary until a direct flakeref is found. If an indirect flakeref cannot be found in any registry, an error will be thrown.

See the [`nix registry` manual page](./nix3-registry.md) for more details on the registry.

)""