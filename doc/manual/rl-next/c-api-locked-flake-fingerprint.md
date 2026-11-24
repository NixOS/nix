---
synopsis: "C API: `nix_locked_flake_get_fingerprint`"
prs: [16673]
---

The new function `nix_locked_flake_get_fingerprint()` in `nix_api_flake.h` returns the fingerprint of a locked flake,
the key under which Nix stores the flake's evaluation cache.
For a flake whose source is not locked, the callback receives the empty string.
