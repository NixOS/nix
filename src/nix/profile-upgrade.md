R""(

# Examples

* Upgrade all packages that were installed using a mutable flake
  reference:

  ```console
  # nix profile upgrade '.*'
  ```

* Upgrade a specific package:

  ```console
  # nix profile upgrade packages.x86_64-linux.hello
  ```

* Upgrade a specific profile element by number:

  ```console
  # nix profile list
  0 flake:nixpkgs#legacyPackages.x86_64-linux.spotify …

  # nix profile upgrade 0
  ```

# Description

This command upgrades a previously installed package in a Nix profile,
by fetching and evaluating the latest version of the flake from which
the package was installed.

> **Warning**
>
> This only works if you used a *mutable* flake reference at
> installation time, e.g. `nixpkgs#hello`. It does not work if you
> used an *immutable* flake reference
> (e.g. `github:NixOS/nixpkgs/13d0c311e3ae923a00f734b43fd1d35b47d8943a#hello`),
> since in that case the "latest version" is always the same.

)""
