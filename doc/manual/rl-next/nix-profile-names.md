---
synopsis: nix profile: Allow referring to elements by human-readable name
prs: 8678
---

[`nix profile`](@docroot@/command-ref/new-cli/nix3-profile.md) now uses names to refer to installed packages when running [`list`](@docroot@/command-ref/new-cli/nix3-profile-list.md), [`remove`](@docroot@/command-ref/new-cli/nix3-profile-remove.md) or [`upgrade`](@docroot@/command-ref/new-cli/nix3-profile-upgrade.md) as opposed to indices. Indices are deprecated and will be removed in a future version.
