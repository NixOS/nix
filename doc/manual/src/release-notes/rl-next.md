# Release X.Y (202?-??-??)

- The experimental nix command is now a `#!-interpreter` by appending the
  contents of any `#! nix` lines and the script's location to a single call.
- [URL flake references](@docroot@/command-ref/new-cli/nix3-flake.md#flake-references) now support [percent-encoded](https://datatracker.ietf.org/doc/html/rfc3986#section-2.1) characters.

- [Path-like flake references](@docroot@/command-ref/new-cli/nix3-flake.md#path-like-syntax) now accept arbitrary unicode characters (except `#` and `?`).

- The experimental feature `repl-flake` is no longer needed, as its functionality is now part of the `flakes` experimental feature. To get the previous behavior, use the `--file/--expr` flags accordingly.

- Introduce new flake installable syntax `flakeref#.attrPath` where the "." prefix denotes no searching of default attribute prefixes like `packages.<SYSTEM>` or `legacyPackages.<SYSTEM>`.

- Nix adds `apple-virt` to the default system features on macOS systems that support virtualization. This is similar to what's done for the `kvm` system feature on Linux hosts.

- Introduce a new built-in function [`builtins.convertHash`](@docroot@/language/builtins.md#builtins-convertHash).

- `nix-shell` shebang lines now support single-quoted arguments.

- `builtins.fetchTree` is now marked as stable.

- The interface for creating and updating lock files has been overhauled:

  - [`nix flake lock`](@docroot@/command-ref/new-cli/nix3-flake-lock.md) only creates lock files and adds missing inputs now.
    It will *never* update existing inputs.

  - [`nix flake update`](@docroot@/command-ref/new-cli/nix3-flake-update.md) does the same, but *will* update inputs.
    - Passing no arguments will update all inputs of the current flake, just like it already did.
    - Passing input names as arguments will ensure only those are updated. This replaces the functionality of `nix flake lock --update-input`
    - To operate on a flake outside the current directory, you must now pass `--flake path/to/flake`.

  - The flake-specific flags `--recreate-lock-file` and `--update-input` have been removed from all commands operating on installables.
    They are superceded by `nix flake update`.
  
- Commit signature verification for the [`builtins.fetchGit`](@docroot@/language/builtins.md#builtins-fetchGit) is added as the new [`verified-fetches` experimental feature](@docroot@/contributing/experimental-features.md#xp-feature-verified-fetches).
