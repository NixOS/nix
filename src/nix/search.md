R""(

# Examples

* Show all packages in the `nixpkgs` flake:

  ```console
  # nix search nixpkgs
  * legacyPackages.x86_64-linux.AMB-plugins (0.8.1)
    A set of ambisonics ladspa plugins

  * legacyPackages.x86_64-linux.ArchiSteamFarm (4.3.1.0)
    Application with primary purpose of idling Steam cards from multiple accounts simultaneously
  …
  ```

* Show packages in the `nixpkgs` flake containing `blender` in its
  name or description:

  ```console
  # nix search nixpkgs blender
  * legacyPackages.x86_64-linux.blender (2.91.0)
    3D Creation/Animation/Publishing System
  ```

* Search for packages underneath the attribute `gnome3` in Nixpkgs:

  ```console
  # nix search nixpkgs#gnome3 vala
  * legacyPackages.x86_64-linux.gnome3.vala (0.48.9)
    Compiler for GObject type system
  ```

* Show all packages in the flake in the current directory:

  ```console
  # nix search
  ```

* Search for Firefox or Chromium:

  ```console
  # nix search nixpkgs 'firefox|chromium'
  ```

* Search for packages containing `git'`and either `frontend` or `gui`:

  ```console
  # nix search nixpkgs git 'frontend|gui'
  ```

# Description

`nix search` searches *installable* (which must be evaluatable, e.g. a
flake) for packages whose name or description matches all of the
regular expressions *regex*.  For each matching package, It prints the
full attribute name (from the root of the installable), the version
and the `meta.description` field, highlighting the substrings that
were matched by the regular expressions. If no regular expressions are
specified, all packages are shown.

# Flake output attributes

If no flake output attribute is given, `nix search` searches for
packages:

* Directly underneath `packages.<system>`.

* Underneath `legacyPackages.<system>`, recursing into attribute sets
  that contain an attribute `recurseForDerivations = true`.

)""
