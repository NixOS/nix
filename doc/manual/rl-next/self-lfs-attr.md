---
synopsis: "`inputs.self.lfs` flake attribute"
prs: [12468]
---

Flakes in Git repositories can now declare that they need Git lfs to be enabled:
```
{
  inputs.self.lfs = true;
}
```
Thus, it's no longer needed for the caller of the flake to pass `lfs = true`.
