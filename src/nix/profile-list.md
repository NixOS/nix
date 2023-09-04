R""(

# Examples

* Show what packages are installed in the default profile:

  ```console
  # nix profile list
  0 flake:nixpkgs#legacyPackages.x86_64-linux.spotify github:NixOS/nixpkgs/c23db78bbd474c4d0c5c3c551877523b4a50db06#legacyPackages.x86_64-linux.spotify /nix/store/akpdsid105phbbvknjsdh7hl4v3fhjkr-spotify-1.1.46.916.g416cacf1
  1 flake:nixpkgs#legacyPackages.x86_64-linux.zoom-us github:NixOS/nixpkgs/c23db78bbd474c4d0c5c3c551877523b4a50db06#legacyPackages.x86_64-linux.zoom-us /nix/store/89pmjmbih5qpi7accgacd17ybpgp4xfm-zoom-us-5.4.53350.1027
  2 flake:blender-bin#packages.x86_64-linux.default github:edolstra/nix-warez/d09d7eea893dcb162e89bc67f6dc1ced14abfc27?dir=blender#packages.x86_64-linux.default /nix/store/zfgralhqjnam662kqsgq6isjw8lhrflz-blender-bin-2.91.0
  ```

# Description

This command shows what packages are currently installed in a
profile. The output consists of one line per package, with the
following fields:

* An integer that can be used to unambiguously identify the package in
  invocations of `nix profile remove` and `nix profile upgrade`.

* The original ("mutable") flake reference and output attribute path
  used at installation time.

* The immutable flake reference to which the mutable flake reference
  was resolved.

* The store path(s) of the package.

)""
