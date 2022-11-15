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
