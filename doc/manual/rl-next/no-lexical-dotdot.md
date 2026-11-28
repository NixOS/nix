---
synopsis: "`..` in paths is no longer removed lexically in several places"
prs: [16890]
---

Removing `a/..` without looking at the file system, as Nix did in many places, is wrong when `a` is a symlink: `a/..` is then the parent of the link's target, not the directory containing the link.
Most places now either resolve the symlinks that `..` depends on, or reject `..` outright where it should not appear:

- A tarball entry or hard link target containing `..`, like `a/../b`, is now an error, naming the archive member, rather than unpacking as `b`.
  This matches what already happened when unpacking tarballs to disk.

- The [`store`](@docroot@/command-ref/conf-file.md#conf-store) directory may no longer contain `..` components; `/foo/bar/../store` is an error rather than `/foo/store`.

- `nix nar ls` follows symlinks within the NAR when resolving `..` in its path.

- `nix-env` resolves `..` in profile and expression paths against the file system.
  It also now takes a relative profile path (e.g. `NIX_PROFILE=test`) relative to the current directory, rather than to `/`.

The Nix language itself is unchanged, still removing `..` lexically for backwards compatibility; see [#16892](https://github.com/NixOS/nix/issues/16892).
