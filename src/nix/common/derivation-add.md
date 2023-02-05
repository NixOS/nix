R""(

# Description

This command reads from standard input a JSON representation of a
[store derivation] to which an [*installable*](./nix.md#installables) evaluates.

Store derivations are used internally by Nix. They are store paths with
extension `.drv` that represent the build-time dependency graph to which
a Nix expression evaluates.

[store derivation]: ../../glossary.md#gloss-store-derivation

The JSON format is documented under the [`derivation show`] command.

[`derivation show`]: ./nix3-derivation-show.md

)""
