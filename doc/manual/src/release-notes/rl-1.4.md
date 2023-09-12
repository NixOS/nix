# Release 1.4 (2013-02-26)

This release fixes a security bug in multi-user operation. It was
possible for derivations to cause the mode of files outside of the Nix
store to be changed to 444 (read-only but world-readable) by creating
hard links to those files
([details](https://github.com/NixOS/nix/commit/5526a282b5b44e9296e61e07d7d2626a79141ac4)).

There are also the following improvements:

  - New built-in function: `builtins.hashString`.

  - Build logs are now stored in `/nix/var/log/nix/drvs/XX/`, where *XX*
    is the first two characters of the derivation. This is useful on
    machines that keep a lot of build logs (such as Hydra servers).

  - The function `corepkgs/fetchurl` can now make the downloaded file
    executable. This will allow getting rid of all bootstrap binaries in
    the Nixpkgs source tree.

  - Language change: The expression `"${./path}
            ..."` now evaluates to a string instead of a path.
