---
synopsis: Terminate active builds
prs: 13813
---

The new [`nix store kill-build`](@docroot@/command-ref/new-cli/nix3-store-kill-build.md) command asks the Nix process holding the output locks for a specified output path or registered derivation to terminate.
It does not remove lock files.
Output locks are also used during substitution and registration, so the command can terminate those operations.
A Nix process can manage multiple builds, so the command can also cancel other builds managed by that process.
