R""(

# Examples

* Apply the build environment of GNU hello to the current shell:

  ```console
  # . <(nix print-dev-env nixpkgs#hello)
  ```

* Get the build environment in JSON format:

  ```console
  # nix print-dev-env nixpkgs#hello --json
  ```

  The output will look like this:

  ```json
  {
    "bashFunctions": {
      "buildPhase": " \n    runHook preBuild;\n...",
      ...
    },
    "variables": {
      "src": {
        "type": "exported",
        "value": "/nix/store/3x7dwzq014bblazs7kq20p9hyzz0qh8g-hello-2.10.tar.gz"
      },
      "postUnpackHooks": {
        "type": "array",
        "value": ["_updateSourceDateEpochFromSourceRoot"]
      },
      ...
    }
  }
  ```

# Description

This command prints a shell script that can be sourced by `bash` and
that sets the variables and shell functions defined by the build
process of *installable*. This allows you to get a similar build
environment in your current shell rather than in a subshell (as with
`nix develop`).

With `--json`, the output is a JSON serialisation of the variables and
functions defined by the build process.

)""
