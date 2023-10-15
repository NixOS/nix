# Lookup path

> **Syntax**
>
> *lookup-path* = `<` *identifier* [ `/` *identifier* ]... `>`

A lookup path is an identifier with an optional path suffix that resolves to a [path value](@docroot@/language/values.md#type-path) if the identifier matches a search path entry.

The value of a lookup path is determined by [`builtins.nixPath`](@docroot@/language/builtin-constants.md#builtins-nixPath).

See [`builtins.findFile`](@docroot@/language/builtins.md#builtins-findFile) for details on lookup path resolution.

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
