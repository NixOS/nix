R""(

# Examples

* Upgrade all packages that were installed using an unlocked flake
  reference:

  ```console
  # nix profile upgrade --all
  ```

* Upgrade a specific package by name:

  ```console
  # nix profile upgrade hello
  ```

* Upgrade all packages that include 'vim' in their name:

  ```console
  # nix profile upgrade --regex '.*vim.*'
  ```

# Description

This command upgrades a previously installed package in a Nix profile,
by fetching and evaluating the latest version of the flake from which
the package was installed.

> **Warning**
>
> This only works if you used an *unlocked* flake reference at
> installation time, e.g. `nixpkgs#hello`. It does not work if you
> used a *locked* flake reference
> (e.g. `github:NixOS/nixpkgs/13d0c311e3ae923a00f734b43fd1d35b47d8943a#hello`),
> since in that case the "latest version" is always the same.

)""
