---
synopsis: "`nix copy` supports `--profile` and `--out-link`"
prs: [11657]
---

The `nix copy` command now has flags `--profile` and `--out-link`, similar to `nix build`. `--profile` makes a profile point to the
top-level store path, while `--out-link` create symlinks to the top-level store paths.

For example, when updating the local NixOS system profile from a NixOS system closure on a remote machine, instead of
```
# nix copy --from ssh://server $path
# nix build --profile /nix/var/nix/profiles/system $path
```
you can now do
```
# nix copy --from ssh://server --profile /nix/var/nix/profiles/system $path
```
The advantage is that this avoids a time window where *path* is not a garbage collector root, and so could be deleted by a concurrent `nix store gc` process.
