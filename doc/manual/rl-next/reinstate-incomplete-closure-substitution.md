---
synopsis: Substitute outputs with an incomplete closure after building their dependencies
issues: [77]
prs: []
---

When a substituter advertises a derivation output but is missing some of the store objects
that output depends on (an "incomplete closure", e.g. because a binary cache garbage-collected
a deep dependency), Nix now builds the missing dependencies from source and then substitutes
the output, instead of rebuilding the output and everything downstream of the hole.

This reinstates the practical effect of the feature removed in commit
`99cb85cd37cf2c086bb9c01ec4b99077e5b866e5` (see the "Revert incomplete closure mixed download
and build feature" note), but with a simpler implementation: the output substitution is retried
once, inline, after the derivation's inputs have been realised. It does not reintroduce the
`ecIncompleteClosure` exit code, the retry state machine, or any re-entrancy in the scheduler.
