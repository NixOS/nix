R""(

# Examples

* To list the contents of a store path in a binary cache:

  ```console
  # nix store ls --store https://cache.nixos.org/ -lR /nix/store/0i2jd68mp5g6h2sa5k9c85rb80sn8hi9-hello-2.10
  dr-xr-xr-x                    0 ./bin
  -r-xr-xr-x                38184 ./bin/hello
  dr-xr-xr-x                    0 ./share
  …
  ```

* To show information about a specific file in a binary cache:

  ```console
  # nix store ls --store https://cache.nixos.org/ -l /nix/store/0i2jd68mp5g6h2sa5k9c85rb80sn8hi9-hello-2.10/bin/hello
  -r-xr-xr-x                38184 hello
  ```

# Description

This command shows information about *path* in a Nix store. *path* can
be a top-level store path or any file inside a store path.

)""
