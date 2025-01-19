R""(

# Examples

* Show the [derivation] that results from evaluating the Hello
  package:

  ```console
  # nix derivation show nixpkgs#hello
  {
    "/nix/store/s6rn4jz1sin56rf4qj5b5v8jxjm32hlk-hello-2.10.drv": {
      …
    }
  }
  ```

* Show the full derivation graph (if available) that produced your
  NixOS system:

  ```console
  # nix derivation show -r /run/current-system
  ```

* Print all files fetched using `fetchurl` by Firefox's dependency
  graph:

  ```console
  # nix derivation show -r nixpkgs#firefox \
    | jq -r '.[] | select(.outputs.out.hash and .env.urls) | .env.urls' \
    | uniq | sort
  ```

  Note that `.outputs.out.hash` selects *fixed-output derivations*
  (derivations that produce output with a specified content hash),
  while `.env.urls` selects derivations with a `urls` attribute.

# Description

This command prints on standard output a JSON representation of the
[derivation]s to which [*installables*](./nix.md#installables) evaluate.

Store derivations are used internally by Nix. They are store paths with
extension `.drv` that represent the build-time dependency graph to which
a Nix expression evaluates.

By default, this command only shows top-level derivations, but with
`--recursive`, it also shows their dependencies.

[derivation]: ../../glossary.md#gloss-derivation

`nix derivation show` outputs a JSON map of [store path]s to derivations in the following format:

[store path]: @docroot@/store/store-path.md

{{#include ../../protocols/json/derivation.md}}

)""
