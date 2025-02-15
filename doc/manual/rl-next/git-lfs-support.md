---
synopsis: "Git LFS support"
prs: [10153]
---

The Git fetcher now supports Large File Storage (LFS). This can be enabled by passing the attribute `lfs = true` to the fetcher, e.g.
```console
nix flake prefetch 'git+ssh://git@github.com/Apress/repo-with-large-file-storage.git?lfs=1'
```

Author: [**@b-camacho**](https://github.com/b-camacho), [**@kip93**](https://github.com/kip93)
