R""(

# Examples

* Download a tarball and unpack it:

  ```console
  # nix store prefetch-tarball https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-5.10.5.tar.xz
  Downloaded 'https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-5.10.5.tar.xz'
  to '/nix/store/sl5vvk8mb4ma1sjyy03kwpvkz50hd22d-source' (hash
  'sha256-3XYHZANT6AFBV0BqegkAZHbba6oeDkIUCDwbATLMhAY=').
  ```

* Download a tarball and unpack it, unless it already exists in the
  Nix store:

  ```console
  # nix store prefetch-tarball https://cdn.kernel.org/pub/linux/kernel/v5.x/linux-5.10.5.tar.xz \
      --expected-hash sha256-3XYHZANT6AFBV0BqegkAZHbba6oeDkIUCDwbATLMhAY=
  ```

# Description

This command downloads a tarball or zip file from *url*, unpacks it,
and adds the unpacked tree to the Nix store. It prints out the
resulting store path and the NAR hash of that store path.

The name component of the store path defaults to `source`, but this
can be overriden using `--name`.

)""
