R""(

# Examples

* Show the store derivation that results from evaluating the Hello
  package:

  ```console
  # nix show-derivation nixpkgs#hello
  {
    "/nix/store/s6rn4jz1sin56rf4qj5b5v8jxjm32hlk-hello-2.10.drv": {
      …
    }
  }
  ```

* Show the full derivation graph (if available) that produced your
  NixOS system:

  ```console
  # nix show-derivation -r /run/current-system
  ```

* Print all files fetched using `fetchurl` by Firefox's dependency
  graph:

  ```console
  # nix show-derivation -r nixpkgs#firefox \
    | jq -r '.[] | select(.outputs.out.hash and .env.urls) | .env.urls' \
    | uniq | sort
  ```

  Note that `.outputs.out.hash` selects *fixed-output derivations*
  (derivations that produce output with a specified content hash),
  while `.env.urls` selects derivations with a `urls` attribute.

# Description

This command prints on standard output a JSON representation of the
store derivations to which *installables* evaluate. Store derivations
are used internally by Nix. They are store paths with extension `.drv`
that represent the build-time dependency graph to which a Nix
expression evaluates.

By default, this command only shows top-level derivations, but with
`--recursive`, it also shows their dependencies.

The JSON output is a JSON object whose keys are the store paths of the
derivations, and whose values are a JSON object with the following
fields:

* `outputs`: Information about the output paths of the
  derivation. This is a JSON object with one member per output, where
  the key is the output name and the value is a JSON object with these
  fields:

  * `path`: The output path.
  * `hashAlgo`: For fixed-output derivations, the hashing algorithm
    (e.g. `sha256`), optionally prefixed by `r:` if `hash` denotes a
    NAR hash rather than a flat file hash.
  * `hash`: For fixed-output derivations, the expected content hash in
    base-16.

  Example:

  ```json
  "outputs": {
    "out": {
      "path": "/nix/store/2543j7c6jn75blc3drf4g5vhb1rhdq29-source",
      "hashAlgo": "r:sha256",
      "hash": "6fc80dcc62179dbc12fc0b5881275898f93444833d21b89dfe5f7fbcbb1d0d62"
    }
  }
  ```

* `inputSrcs`: A list of store paths on which this derivation depends.

* `inputDrvs`: A JSON object specifying the derivations on which this
  derivation depends, and what outputs of those derivations. For
  example,

  ```json
  "inputDrvs": {
    "/nix/store/6lkh5yi7nlb7l6dr8fljlli5zfd9hq58-curl-7.73.0.drv": ["dev"],
    "/nix/store/fn3kgnfzl5dzym26j8g907gq3kbm8bfh-unzip-6.0.drv": ["out"]
  }
  ```

  specifies that this derivation depends on the `dev` output of
  `curl`, and the `out` output of `unzip`.

* `system`: The system type on which this derivation is to be built
  (e.g. `x86_64-linux`).

* `builder`: The absolute path of the program to be executed to run
  the build. Typically this is the `bash` shell
  (e.g. `/nix/store/r3j288vpmczbl500w6zz89gyfa4nr0b1-bash-4.4-p23/bin/bash`).

* `args`: The command-line arguments passed to the `builder`.

* `env`: The environment passed to the `builder`.

)""
