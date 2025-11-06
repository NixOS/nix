# Release 2.19 (2023-11-17)

- The experimental `nix` command can now act as a [shebang interpreter](@docroot@/command-ref/new-cli/nix.md#shebang-interpreter)
  by appending the contents of any `#! nix` lines and the script's location into a single call.

- [URL flake references](@docroot@/command-ref/new-cli/nix3-flake.md#flake-references) now support [percent-encoded](https://datatracker.ietf.org/doc/html/rfc3986#section-2.1) characters.

- [Path-like flake references](@docroot@/command-ref/new-cli/nix3-flake.md#path-like-syntax) now accept arbitrary unicode characters (except `#` and `?`).

- The experimental feature `repl-flake` is no longer needed, as its functionality is now part of the `flakes` experimental feature. To get the previous behavior, use the `--file/--expr` flags accordingly.

- There is a new flake installable syntax `flakeref#.attrPath` where the "." prefix specifies that `attrPath` is interpreted from the root of the flake outputs, with no searching of default attribute prefixes like `packages.<SYSTEM>` or `legacyPackages.<SYSTEM>`.

- Nix adds `apple-virt` to the default system features on macOS systems that support virtualization. This is similar to what's done for the `kvm` system feature on Linux hosts.

- Add a new built-in function [`builtins.convertHash`](@docroot@/language/builtins.md#builtins-convertHash).

- `nix-shell` shebang lines now support single-quoted arguments.

- `builtins.fetchTree` is now its own experimental feature, [`fetch-tree`](@docroot@/development/experimental-features.md#xp-feature-fetch-tree).
  This allows stabilising it independently of the rest of what is encompassed by [`flakes`](@docroot@/development/experimental-features.md#xp-feature-flakes).

- The interface for creating and updating lock files has been overhauled:

  - [`nix flake lock`](@docroot@/command-ref/new-cli/nix3-flake-lock.md) only creates lock files and adds missing inputs now.
    It will *never* update existing inputs.

  - [`nix flake update`](@docroot@/command-ref/new-cli/nix3-flake-update.md) does the same, but *will* update inputs.
    - Passing no arguments will update all inputs of the current flake, just like it already did.
    - Passing input names as arguments will ensure only those are updated. This replaces the functionality of `nix flake lock --update-input`
    - To operate on a flake outside the current directory, you must now pass `--flake path/to/flake`.

  - The flake-specific flags `--recreate-lock-file` and `--update-input` have been removed from all commands operating on installables.
    They are superseded by `nix flake update`.

- Commit signature verification for the [`builtins.fetchGit`](@docroot@/language/builtins.md#builtins-fetchGit) is added as the new [`verified-fetches` experimental feature](@docroot@/development/experimental-features.md#xp-feature-verified-fetches).

- [`nix path-info --json`](@docroot@/command-ref/new-cli/nix3-path-info.md)
  (experimental) now returns a JSON map rather than JSON list.
  The `path` field of each object has instead become the key in the outer map, since it is unique.
  The `valid` field also goes away because we just use `null` instead.

  - Old way:

    ```json5
    [
      {
        "path": "/nix/store/8fv91097mbh5049i9rglc73dx6kjg3qk-bash-5.2-p15",
        "valid": true,
        // ...
      },
      {
        "path": "/nix/store/wffw7l0alvs3iw94cbgi1gmmbmw99sqb-home-manager-path",
        "valid": false
      }
    ]
    ```

  - New way

    ```json5
    {
      "/nix/store/8fv91097mbh5049i9rglc73dx6kjg3qk-bash-5.2-p15": {
        // ...
      },
      "/nix/store/wffw7l0alvs3iw94cbgi1gmmbmw99sqb-home-manager-path": null,
    }
    ```

  This makes it match `nix derivation show`, which also maps store paths to information.

- When Nix is installed using the [binary installer](@docroot@/installation/installing-binary.md), in supported shells (Bash, Zsh, Fish)
  [`XDG_DATA_DIRS`](https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html#variables) is now populated with the path to the `/share` subdirectory of the current profile.
  This means that command completion scripts, `.desktop` files, and similar artifacts installed via [`nix-env`](@docroot@/command-ref/nix-env.md) or [`nix profile`](@docroot@/command-ref/new-cli/nix3-profile.md)
  (experimental) can be found by any program that follows the [XDG Base Directory Specification](https://specifications.freedesktop.org/basedir-spec/basedir-spec-latest.html).

- A new command `nix store add` has been added. It replaces `nix store add-file` and `nix store add-path` which are now deprecated.

- A new option [`always-allow-substitutes`](@docroot@/command-ref/conf-file.md#conf-always-allow-substitutes) has been added.

  When set to `true`, Nix will always try to substitute a derivation, even if it has the [`allowSubstitutes`]{#adv-attr-allowSubstitutes} attribute set to `false`.
