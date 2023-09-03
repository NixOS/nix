R""(

# Examples

* Download a tarball and unpack it:

  ```console
  # nix flake prefetch https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-5.10.5.tar.xz
  Downloaded 'https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-5.10.5.tar.xz?narHash=sha256-3XYHZANT6AFBV0BqegkAZHbba6oeDkIUCDwbATLMhAY='
  to '/nix/store/sl5vvk8mb4ma1sjyy03kwpvkz50hd22d-source' (hash
  'sha256-3XYHZANT6AFBV0BqegkAZHbba6oeDkIUCDwbATLMhAY=').
  ```

* Download the `dwarffs` flake (looked up in the flake registry):

  ```console
  # nix flake prefetch dwarffs --json
  {"hash":"sha256-VHg3MYVgQ12LeRSU2PSoDeKlSPD8PYYEFxxwkVVDRd0="
  ,"storePath":"/nix/store/hang3792qwdmm2n0d9nsrs5n6bsws6kv-source"}
  ```

# Description

This command downloads the source tree denoted by flake reference
*flake-url*. Note that this does not need to be a flake (i.e. it does
not have to contain a `flake.nix` file).

)""
