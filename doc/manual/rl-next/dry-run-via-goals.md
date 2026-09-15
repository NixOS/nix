---
synopsis: "`--dry-run` uses the same logic as a real build"
prs: []
issues: []
---

Working out what a build would do (`--dry-run`, and the "these derivations will be built" / "these paths will be fetched" messages) is no longer a separate re-implementation of the build scheduler's decisions.
Instead, the build scheduler itself runs in a dry-run mode, in which it stops right before it would substitute or build anything and reports that instead.

As a result, the report matches what would actually happen more closely.
For instance, dynamic derivations are no longer skipped with a warning, and a derivation whose `.drv` file is not present is reported as needing to be fetched rather than as unknown.
