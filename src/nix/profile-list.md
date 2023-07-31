R""(

# Examples

* Show what packages are installed in the default profile:

  ```console
  # nix profile list
  Index:              0
  Flake attribute:    legacyPackages.x86_64-linux.gdb
  Original flake URL: flake:nixpkgs
  Locked flake URL:   github:NixOS/nixpkgs/7b38b03d76ab71bdc8dc325e3f6338d984cc35ca
  Store paths:        /nix/store/indzcw5wvlhx6vwk7k4iq29q15chvr3d-gdb-11.1

  Index:              1
  Flake attribute:    packages.x86_64-linux.default
  Original flake URL: flake:blender-bin
  Locked flake URL:   github:edolstra/nix-warez/91f2ffee657bf834e4475865ae336e2379282d34?dir=blender
  Store paths:        /nix/store/i798sxl3j40wpdi1rgf391id1b5klw7g-blender-bin-3.1.2
  ```

  Note that you can unambiguously rebuild a package from a profile
  through its locked flake URL and flake attribute, e.g.

  ```console
  # nix build github:edolstra/nix-warez/91f2ffee657bf834e4475865ae336e2379282d34?dir=blender#packages.x86_64-linux.default
  ```

  will build the package with index 1 shown above.

# Description

This command shows what packages are currently installed in a
profile. For each installed package, it shows the following
information:

* `Index`: An integer that can be used to unambiguously identify the
  package in invocations of `nix profile remove` and `nix profile
  upgrade`.

* `Flake attribute`: The flake output attribute path that provides the
  package (e.g. `packages.x86_64-linux.hello`).

* `Original flake URL`: The original ("unlocked") flake reference
  specified by the user when the package was first installed via `nix
  profile install`.

* `Locked flake URL`: The locked flake reference to which the original
  flake reference was resolved.

* `Store paths`: The store path(s) of the package.

)""
