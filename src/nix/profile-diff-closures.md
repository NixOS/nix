R""(

# Examples

* Show what changed between each version of the NixOS system
  profile:

  ```console
  # nix profile diff-closures --profile /nix/var/nix/profiles/system
  Version 13 -> 14:
    acpi-call: 2020-04-07-5.8.13 → 2020-04-07-5.8.14
    aws-sdk-cpp: -6723.1 KiB
    …

  Version 14 -> 15:
    acpi-call: 2020-04-07-5.8.14 → 2020-04-07-5.8.16
    attica: -996.2 KiB
    breeze-icons: -78713.5 KiB
    brotli: 1.0.7 → 1.0.9, +44.2 KiB
  ```

# Description

This command shows the difference between the closures of subsequent
versions of a profile. See [`nix store
diff-closures`](nix3-store-diff-closures.md) for details.

)""
