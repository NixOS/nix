R""(

# Examples

* To serialise directory `foo` as a NAR:

  ```console
  # nix nar dump-path ./foo > foo.nar
  ```

# Description

This command generates a NAR file containing the serialisation of
*path*, which must contain only regular files, directories and
symbolic links. The NAR is written to standard output.

)""
