R""(

# Description

This command reads from standard input a JSON representation of a
[store derivation].

Store derivations are used internally by Nix. They are store paths with
extension `.drv` that represent the build-time dependency graph to which
a Nix expression evaluates.


[store derivation]: @docroot@/glossary.md#gloss-store-derivation

`nix derivation add` takes a single derivation in the JSON format.
See [the manual](@docroot@/protocols/json/derivation.md) for a documentation of this format.

)""
