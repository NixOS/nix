---
synopsis: "Build cgroups are named after the derivation"
prs: [16516]
---

Per-build cgroups are now named `nix-build@<drv-hash>-<uid>` when a build user is used, and `nix-build@<drv-hash>-<pid>` otherwise.
They replace `nix-build-uid-<uid>` and `nix-build-pid-<pid>-<counter>`, so external tools can locate the cgroup of a given derivation's build.
