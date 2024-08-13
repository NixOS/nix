# Lookup path

> **Syntax**
>
> *lookup-path* = `<` *identifier* [ `/` *identifier* ]... `>`

A lookup path is an identifier with an optional path suffix that resolves to a file system [path value](@docroot@/language/types.md#type-path) if the identifier matches a search path entry.

The value of a lookup path is determined by [`builtins.nixPath`](@docroot@/language/builtins.md#builtins-nixPath).

See [`builtins.findFile`](@docroot@/language/builtins.md#builtins-findFile) for details on lookup path resolution.

> **Example**
>
> Generally, `<nixpkgs>` points to the file system path of some revision of [Nixpkgs](https://nix.dev/reference/glossary#term-Nixpkgs).
> 
> ```nix
> <nixpkgs>
>```
>
>     /nix/var/nix/profiles/per-user/root/channels/nixpkgs

> **Example**
>
> `<nixpkgs/lib>` points to the subdirectory `nixos` of the file system path `<nixpkgs>` points to:
> 
> ```nix
> <nixpkgs/nixos>
>```
>
>     /nix/var/nix/profiles/per-user/root/channels/nixpkgs/nixos
