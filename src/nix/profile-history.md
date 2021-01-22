R""(

# Examples

* Show the changes between each version of your default profile:

  ```console
  # nix profile history
  Version 508 -> 509:
    flake:nixpkgs#legacyPackages.x86_64-linux.awscli: ∅ -> 1.17.13

  Version 509 -> 510:
    flake:nixpkgs#legacyPackages.x86_64-linux.awscli: 1.17.13 -> 1.18.211
  ```

# Description

This command shows what packages were added, removed or upgraded
between subsequent versions of a profile. It only shows top-level
packages, not dependencies; for that, use [`nix profile
diff-closures`](./nix3-profile-diff-closures.md).

The addition of a package to a profile is denoted by the string `∅ ->`
*version*, whereas the removal is denoted by *version* `-> ∅`.

)""
