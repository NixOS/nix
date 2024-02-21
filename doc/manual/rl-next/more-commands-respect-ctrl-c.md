---
synopsis: Nix commands respect Ctrl-C
prs: 9687 6995
issues: 7245
---

Previously, many Nix commands would hang indefinitely if Ctrl-C was pressed
while performing various operations (including `nix develop`, `nix flake
update`, and so on). With several fixes to Nix's signal handlers, Nix commands
will now exit quickly after Ctrl-C is pressed.

This was actually released in Nix 2.20, but wasn't added to the release notes
so we're announcing it here. The historical release notes have been updated as well.
