---
synopsis: "`nix-shell <directory>` looks for `shell.nix`"
significance: significant
issues:
- 496
- 2279
- 4529
- 5431
- 11053
prs:
- 11057
---

`nix-shell $x` now looks for `$x/shell.nix` when `$x` resolves to a directory.

Although this might be seen as a breaking change, its primarily interactive usage makes it a minor issue.
This adjustment addresses a commonly reported problem.

This also applies to `nix-shell` shebang scripts. Consider the following example:

```shell
#!/usr/bin/env nix-shell
#!nix-shell -i bash
```

This will now load `shell.nix` from the script's directory, if it exists; `default.nix` otherwise.

The old behavior can be opted into by setting the option [`nix-shell-always-looks-for-shell-nix`](@docroot@/command-ref/conf-file.md#conf-nix-shell-always-looks-for-shell-nix) to `false`.
