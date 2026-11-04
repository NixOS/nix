---
synopsis: "`nix eval --write-to` is stricter about what it writes"
prs: [16760]
---

`nix eval --write-to` now requires attributes to correspond to a single file name.
In particular, it rejects attribute names that contain `/` or are empty, in addition to rejecting `.` and `..` as it did already.
Previously, an attribute such as `{ "a/b" = "..."; }` was written to a nested path `a/b`, which is a bit of an "injection".

Attribute sets can also now be nested at most 64 levels deep, the same limit as for NARs.

(The output is also now written in the same way as NARs are unpacked, which hardens it in a bunch of ways.)
