R""(

# Examples

* Show the contents of a file in a binary cache:

  ```console
  # nix store cat --store https://cache.nixos.org/ \
      /nix/store/qbhyj3blxpw2i6pb7c6grc9185nbnpvy-hello-2.10/bin/hello | hexdump -C | head -n1
  00000000  7f 45 4c 46 02 01 01 00  00 00 00 00 00 00 00 00  |.ELF............|
  ```

# Description

This command prints on standard output the contents of the regular
file *path* in a Nix store. *path* can be a top-level store path or
any file inside a store path.

)""
