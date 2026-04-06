---
synopsis: macOS Spotlight integration for Nix-installed `.app` bundles
issues: [7055]
---

On macOS, profile-installed `.app` bundles now appear in Spotlight, `mdfind`, and `open -a Foo`.

Nix creates a small shadow bundle under a profile-specific subdirectory of `~/Applications/Nix Profile Apps/` (or `/Applications/Nix Profile Apps/` when run as root).
It copies `Info.plist`, `PkgInfo` if present, and the icon file.
The executable, frameworks, and other resources remain symlinks into `/nix/store`.

See [macOS Spotlight Integration](@docroot@/installation/macos-spotlight-apps.md) for details.
