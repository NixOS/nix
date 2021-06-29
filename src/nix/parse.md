R""(

# Examples

* Parse a Nix file:

  ```console
  # nix parse some-file.nix
  ```

* Parse a Nix file to JSON format:

  ```console
  # nix parse --output-format json some-file.nix
  ```

# Description

This command parses the Nix expression *installable* and prints the
result on standard output.

# Output format

`nix parse` can produce output in several formats:

* By default, the parse result is printed in ATerm format.

* With `--output-format json`, the parse result is printed in JSON format.

)""
