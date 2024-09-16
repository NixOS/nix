---
synopsis: Add setting `fsync-store-paths`
issues: [1218]
prs: [7126]
---

Nix now has a setting `fsync-store-paths` that ensures that new store paths are durably written to disk before they are registered as "valid" in Nix's database. This can prevent Nix store corruption if the system crashes or there is a power loss. This setting defaults to `false`.

Author: [**@squalus**](https://github.com/squalus)
