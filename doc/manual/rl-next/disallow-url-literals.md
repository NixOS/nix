---
synopsis: Disallow URL literals
---

- URL literals (unquoted URLs) are now disabled by default in the Nix language.
  They were previously optionally disallowable via the `no-url-literals` experimental feature, which has now been removed.
  Existing Nix expressions containing URL literals must be updated to use quoted strings (e.g. `"http://example.com"` instead of `http://example.com`).
  However, for transitional purposes, URL literals can be re-enabled using the new `url-literals` deprecated feature (e.g. `--extra-deprecated-features url-literals`).
