R""(

# Examples

* To copy the build log of the `hello` package from
  https://cache.nixos.org to the local store:

  ```console
  # nix store copy-log --from https://cache.nixos.org --eval-store auto nixpkgs#hello
  ```

  You can verify that the log is available locally:

  ```console
  # nix log --substituters '' nixpkgs#hello
  ```

  (The flag `--substituters ''` avoids querying
  `https://cache.nixos.org` for the log.)

* To copy the log for a specific store derivation via SSH:

  ```console
  # nix store copy-log --to ssh-ng://machine /nix/store/ilgm50plpmcgjhcp33z6n4qbnpqfhxym-glibc-2.33-59.drv
  ```

# Description

`nix store copy-log` copies build logs between two Nix stores. The
source store is specified using `--from` and the destination using
`--to`. If one of these is omitted, it defaults to the local store.

)""
