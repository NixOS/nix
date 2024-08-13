# Lookup path

> **Syntax**
>
> *lookup-path* = `<` *identifier* [ `/` *identifier* ]... `>`

A lookup path is an identifier with an optional path suffix that resolves to a [path value](@docroot@/language/types.md#type-path) if the identifier matches a search path entry in [`builtins.nixPath`](@docroot@/language/builtins.md#builtins-nixPath).
The algorithm for lookup path resolution is described in the documentation on [`builtins.findFile`](@docroot@/language/builtins.md#builtins-findFile).

> **Example**
>
> ```nix
> <nixpkgs>
>```
>
>     /nix/var/nix/profiles/per-user/root/channels/nixpkgs

> **Example**
>
> ```nix
> <nixpkgs/nixos>
>```
>
>     /nix/var/nix/profiles/per-user/root/channels/nixpkgs/nixos
