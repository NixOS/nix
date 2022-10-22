# Release X.Y (202?-??-??)

* `<nix/fetchurl.nix>` now accepts an additional argument `impure` which
  defaults to `false`.  If it is set to `true`, the `hash` and `sha256`
  arguments will be ignored and the resulting derivation will have
  `__impure` set to `true`, making it an impure derivation.
* Error traces have been reworked to provide detailed explanations and more
  accurate error locations. A short excerpt of the trace is now shown by
  default when an error occurs.
