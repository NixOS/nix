# Release X.Y (202?-??-??)

- [URL flake references](@docroot@/command-ref/new-cli/nix3-flake.md#flake-references) now support [percent-encoded](https://datatracker.ietf.org/doc/html/rfc3986#section-2.1) characters.

- [Path-like flake references](@docroot@/command-ref/new-cli/nix3-flake.md#path-like-syntax) now accept arbitrary unicode characters (except `#` and `?`).

- The experimental feature `repl-flake` is no longer needed, as its functionality is now part of the `flakes` experimental feature. To get the previous behavior, use the `--file/--expr` flags accordingly.

- Introduce new flake installable syntax `flakeref#.attrPath` where the "." prefix denotes no searching of default attribute prefixes like `packages.<SYSTEM>` or `legacyPackages.<SYSTEM>`.

- Nix adds `apple-virt` to the default system features on macOS systems that support virtualization. This is similar to what's done for the `kvm` system feature on Linux hosts.

- Introduce a new built-in function [`builtins.convertHash`](@docroot@/language/builtins.md#builtins-convertHash).

- `builtins.fetchTree` is now marked as stable.
