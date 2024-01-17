---
synopsis: "`nix profile` now allows referring to elements by human-readable name"
prs: 8678
---

[`nix profile`](@docroot@/command-ref/new-cli/nix3-profile.md) now uses names to refer to installed packages when running [`list`](@docroot@/command-ref/new-cli/nix3-profile-list.md), [`remove`](@docroot@/command-ref/new-cli/nix3-profile-remove.md) or [`upgrade`](@docroot@/command-ref/new-cli/nix3-profile-upgrade.md) as opposed to indices. Profile element names are generated when a package is installed and remain the same until the package is removed.

**Warning**: The `manifest.nix` file used to record the contents of profiles has changed. Nix will automatically upgrade profiles to the new version when you modify the profile. After that, the profile can no longer be used by older versions of Nix.
