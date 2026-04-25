---
synopsis: macOS Spotlight integration for Nix-installed `.app` bundles
issues: [7055]
---

On macOS, `nix profile install` (and every other profile-switch command) now
makes installed `.app` bundles appear in Spotlight (⌘-Space), in `mdfind`,
and as a target for `open -a Foo`, with their real icon and bundle name.

This is done by materializing a shadow bundle under
`~/Applications/Nix Profile Apps/` (or `/Applications/Nix Profile Apps/` for
the root profile) containing a real `Info.plist` and icon file, with the rest
of `Contents/` symlinked back into `/nix/store`. The executable, frameworks
and resources are never copied out of the store, and garbage collection is
unaffected.

See [macOS Spotlight Integration](@docroot@/installation/macos-spotlight-apps.md)
for the full design.
