---
synopsis: The `flake update` command now has a `--exclude` flag to exclude inputs from an update.
issues: 10215
prs: 16189
---

Use the new flag with to update all inputs except for the specified ones (effectively switches from whitelist to blacklist):

```shell
nix flake update -x nixpkgs home-manager
```
