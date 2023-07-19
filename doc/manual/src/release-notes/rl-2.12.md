# Release 2.12 (2022-12-06)

* On Linux, Nix can now run builds in a user namespace where they run
  as root (UID 0) and have 65,536 UIDs available.

  This can be used by requiring `uid-range` [system feature] in derivations.

  [system feature]: ../command-ref/conf-file.md#conf-system-features

* Nix can now automatically pick UIDs for builds, removing the need to
  create `nixbld*` user accounts. See [`auto-allocate-uids`].

  [`auto-allocate-uids`]: ../command-ref/conf-file.md#conf-auto-allocate-uids

* On Linux, Nix has experimental support for running builds inside a
  cgroup. See
  [`use-cgroups`](../command-ref/conf-file.md#conf-use-cgroups).

* `<nix/fetchurl.nix>` now accepts an additional argument `impure` which
  defaults to `false`.  If it is set to `true`, the `hash` and `sha256`
  arguments will be ignored and the resulting derivation will have
  `__impure` set to `true`, making it an impure derivation.

* If `builtins.readFile` is called on a file with context, then only
  the parts of the context that appear in the content of the file are
  retained.  This avoids a lot of spurious errors where strings end up
  having a context just because they are read from a store path
  ([#7260](https://github.com/NixOS/nix/pull/7260)).

* `nix build --json` now prints some statistics about top-level
  derivations, such as CPU statistics when cgroups are enabled.
