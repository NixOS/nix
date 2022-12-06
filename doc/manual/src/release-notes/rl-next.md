# Release X.Y (202?-??-??)

* `<nix/fetchurl.nix>` now accepts an additional argument `impure` which
  defaults to `false`.  If it is set to `true`, the `hash` and `sha256`
  arguments will be ignored and the resulting derivation will have
  `__impure` set to `true`, making it an impure derivation.

* If `builtins.readFile` is called on a file with context, then only the parts
  of that context that appear in the content of the file are retained.
  This avoids a lot of spurious errors where some benign strings end-up having
  a context just because they are read from a store path
  ([#7260](https://github.com/NixOS/nix/pull/7260)).

* Nix can now automatically pick UIDs for builds, removing the need to
  create `nixbld*` user accounts.

  See [`auto-allocate-uids`].

  [`auto-allocate-uids`]: (../command-ref/conf-file.md#conf-auto-allocate-uids)

* On Linux, Nix can now run builds in a user namespace where the build
  runs as root (UID 0) and has 65,536 UIDs available.

  <!-- FIXME: move this to its own section about system features -->

  This is primarily useful for running containers such as `systemd-nspawn`
  inside a Nix build. For an example, see [`tests/systemd-nspawn/nix`][nspawn].

  [nspawn]: https://github.com/NixOS/nix/blob/67bcb99700a0da1395fa063d7c6586740b304598/tests/systemd-nspawn.nix.

  A build can enable this by by setting the derivation attribute:

  ```
  requiredSystemFeatures = [ "uid-range" ];
  ```

  The `uid-range` [system feature] requires the [`auto-allocate-uids`]
  setting to be enabled.

  [system feature]: (../command-ref/conf-file.md#conf-system-features),

* On Linux, Nix has experimental support for running builds inside a
  cgroup.

  See [`use-cgroups`](../command-ref/conf-file.md#conf-use-cgroups).

* `nix build --json` now prints some statistics about top-level
  derivations, such as CPU statistics when cgroups are enabled.
