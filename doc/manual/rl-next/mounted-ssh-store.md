---
synopsis: Mounted SSH Store
issues: 7890
prs: 7912
---

Introduced the store [`mounted-ssh-ng://`](@docroot@/command-ref/new-cli/nix3-help-stores.md).
This store allows full access to a Nix store on a remote machine and additionally requires that the store be mounted in the local filesystem.
