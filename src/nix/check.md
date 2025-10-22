R""(

# Examples

* Check a package from nixpkgs:

  ```console
  # nix check nixpkgs#hello
  ```

* Check a flake check attribute:

  ```console
  # nix check .#checks.x86_64-linux.integration-test
  ```

* Check multiple derivations:

  ```console
  # nix check .#package-a .#package-b
  ```

# Description

`nix check` verifies that the specified *installables* can be realised.
[Installables](./nix.md#installables) that resolve to derivations are
checked by building them from source, or verifying they can be fetched
from a substituter.

This command requires at least one installable argument with a specific
attribute path (e.g., `nixpkgs#hello` or `.#checks.x86_64-linux.foo`). Bare
flake references without an attribute path are not accepted. If you want to
check all outputs of a flake, use `nix flake check` instead.

Unlike `nix build`, this command:
- Does not create result symlinks
- Does not download outputs that are available in substituters
- Only builds derivations that cannot be substituted

**Note**: When checking substitutability, only metadata is queried - no full
substitution is performed. A broken or unreliable binary cache could still
fail to provide the contents even after `nix check` reports success.

When looking up flake attributes, this command first searches in
`checks.<system>`, then falls back to `packages.<system>` and
`legacyPackages.<system>` (like `nix build`).

This command is useful for CI systems and development workflows where you
want to verify that derivations are buildable without creating local store
contents and result symlinks.

)""
