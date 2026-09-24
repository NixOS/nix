---
synopsis: "Build cgroups are named after the derivation"
prs: [16516]
---

When a build user is used, per-build cgroups are now named
`nix-build@<drv-hash>-<uid>` instead of `nix-build-uid-<uid>`, so external
tools can locate the cgroup of a given derivation's build.
