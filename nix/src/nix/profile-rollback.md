R""(

# Examples

* Roll back your default profile to the previous version:

  ```console
  # nix profile rollback
  switching profile from version 519 to 518
  ```

* Switch your default profile to version 510:

  ```console
  # nix profile rollback --to 510
  switching profile from version 518 to 510
  ```

# Description

This command switches a profile to the most recent version older
than the currently active version, or if `--to` *N* is given, to
version *N* of the profile. To see the available versions of a
profile, use `nix profile history`.

)""
