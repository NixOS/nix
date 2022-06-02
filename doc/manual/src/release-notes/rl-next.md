# Release X.Y (202?-??-??)

* Nix can now be built with LTO by passing `--enable-lto` to `configure`.
  LTO is currently only supported when building with GCC.

* Add experimental *indexed store derivations* installable syntax, part of the
  the `computed-derivations` experimental feature.
