R""(

# Examples

* To serialise directory `foo` as a [Nix Archive (NAR)][Nix Archive]:

  ```console
  # nix nar pack ./foo > foo.nar
  ```

# Description

This command generates a [Nix Archive (NAR)][Nix Archive] file containing the serialisation of
*path*, which must contain only regular files, directories and
symbolic links. The NAR is written to standard output.

[Nix Archive]: @docroot@/store/file-system-object/content-address.md#serial-nix-archive

)""
