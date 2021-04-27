R""(

# Examples

* Download a file to the Nix store:

  ```console
  # nix store prefetch-file https://releases.nixos.org/nix/nix-2.3.10/nix-2.3.10.tar.xz
  Downloaded 'https://releases.nixos.org/nix/nix-2.3.10/nix-2.3.10.tar.xz' to
  '/nix/store/vbdbi42hgnc4h7pyqzp6h2yf77kw93aw-source' (hash
  'sha256-qKheVd5D0BervxMDbt+1hnTKE2aRWC8XCAwc0SeHt6s=').
  ```

* Download a file and get the SHA-512 hash:

  ```console
  # nix store prefetch-file --json --hash-type sha512 \
      https://releases.nixos.org/nix/nix-2.3.10/nix-2.3.10.tar.xz \
    | jq -r .hash
  sha512-6XJxfym0TNH9knxeH4ZOvns6wElFy3uahunl2hJgovACCMEMXSy42s69zWVyGJALXTI+86tpDJGlIcAySEKBbA==
  ```

# Description

This command downloads the file *url* to the Nix store. It prints out
the resulting store path and the cryptographic hash of the contents of
the file.

The name component of the store path defaults to the last component of
*url*, but this can be overridden using `--name`.

)""
