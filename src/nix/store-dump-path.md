R""(

# Examples

* To get a NAR containing the GNU Hello package:

  ```console
  # nix store dump-path nixpkgs#hello > hello.nar
  ```

* To get a NAR from the binary cache https://cache.nixos.org/:

  ```console
  # nix store dump-path --store https://cache.nixos.org/ \
      /nix/store/vyrnv99qi410q82qp7nw7lcl37zmzaxd-glibc-2.25 > glibc.nar
  ```

# Description

This command generates a [Nix Archive (NAR)][Nix Archive] file containing the serialisation of the
store path [*installable*](./nix.md#installables). The NAR is written to standard output.

[Nix Archive]: @docroot@/store/file-system-object/content-address.md#serial-nix-archive

)""
