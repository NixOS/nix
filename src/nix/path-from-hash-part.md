R""(

# Examples

* Return the full store path with the given hash part:

  ```console
  # nix store path-from-hash-part --store https://cache.nixos.org/ qbhyj3blxpw2i6pb7c6grc9185nbnpvy
  /nix/store/qbhyj3blxpw2i6pb7c6grc9185nbnpvy-hello-2.10
  ```

# Description

Given the hash part of a store path (that is, the 32 characters
following `/nix/store/`), return the full store path. This is
primarily useful in the implementation of binary caches, where a
request for a `.narinfo` file only supplies the hash part
(e.g. `https://cache.nixos.org/qbhyj3blxpw2i6pb7c6grc9185nbnpvy.narinfo`).

)""
