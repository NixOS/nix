R""(

# Examples

* Upgrade Nix to the latest stable version:

  ```console
  # nix upgrade-nix
  ```

* Upgrade Nix in a specific profile:

  ```console
  # nix upgrade-nix -p /nix/var/nix/profiles/per-user/alice/profile
  ```

# Description

This command upgrades Nix to the latest version. By default, it
locates the directory containing the `nix` binary in the `$PATH`
environment variable. If that directory is a Nix profile, it will
upgrade the `nix` package in that profile to the latest stable binary
release.

You cannot use this command to upgrade Nix in the system profile of a
NixOS system (that is, if `nix` is found in `/run/current-system`).

)""
