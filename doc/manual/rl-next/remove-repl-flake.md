---
synopsis: Remove experimental repl-flake
significant: significant
issues: 10103
prs: 10299
---

This PR removes the repl-flake feature that was adopted to provide a migration path when changing the behavior of `nix repl`. Moving forward this command will behave more like the rest of the modern cli.

- Removes any repl-flake references.
- Removes the parts of `applyDefaultInstallables` that are no longer needed in repl.cc.
- Fix/Add any tests.
