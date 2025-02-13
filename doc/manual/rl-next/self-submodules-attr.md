---
synopsis: "`inputs.self.submodules` flake attribute"
prs: [12421]
---

Flakes in Git repositories can now declare that they need Git submodules to be enabled:
```
{
  inputs.self.submodules = true;
}
```
Thus, it's no longer needed for the caller of the flake to pass `submodules = true`.
