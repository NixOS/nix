R""(

# Examples

* Download a tarball:

  ```console
  # nix flake prefetch https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-5.10.5.tar.xz
  Fetched 'https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-5.10.5.tar.xz?narHash=sha256-3XYHZANT6AFBV0BqegkAZHbba6oeDkIUCDwbATLMhAY='.
  ```

* Download the `dwarffs` flake (looked up in the flake registry):

  ```console
  # nix flake prefetch dwarffs --json
  {}
  ```

# Description

This command downloads the source tree denoted by flake reference
*flake-url*. Note that this does not need to be a flake (i.e. it does
not have to contain a `flake.nix` file).

)""
