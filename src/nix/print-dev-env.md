R""(

# Examples

* Apply the build environment of GNU hello to the current shell:

  ```console
  # . <(nix print-dev-env nixpkgs#hello)
  ```

# Description

This command prints a shell script that can be sourced by `b`ash and
that sets the environment variables and shell functions defined by the
build process of *installable*. This allows you to get a similar build
environment in your current shell rather than in a subshell (as with
`nix develop`).

)""
