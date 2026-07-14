---
synopsis: Protect build inputs from GC during remote builds
issues: [1970, 2285]
prs: []
---

Remote builds via `ssh-ng://` send a `BasicDerivation` with all dependencies
flattened into `inputSrcs`. The builder-side goal previously skipped
already-valid input paths without registering them as temp GC roots, so a
`min-free` auto-GC on the builder could delete them mid-build. `copyPaths()`
on the client side does root paths it copies, but it skips paths already
present on the builder, leaving precisely those unprotected.

Inputs are now rooted unconditionally at goal start, mirroring what
`PathSubstitutionGoal` already does.
