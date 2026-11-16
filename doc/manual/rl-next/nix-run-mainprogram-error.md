---
synopsis: "Improved error message when `nix run` cannot find a main program"
issues: [15138]
---

When `nix run` is used on a derivation and the binary does not exist, the error message now preserves the original system error and adds context about where the program name was determined from (`meta.mainProgram`, `pname`, or `name`).

Previously, this would produce a confusing OS-level error:

```
error: unable to execute '/nix/store/.../bin/xyz': No such file or directory
```

Now it produces:

```
error:
       … while running program 'xyz' (determined from 'pname') of derivation 'xyz-1.0'

       … consider setting 'meta.mainProgram' or using 'nix shell' instead

       error: unable to execute '/nix/store/.../bin/xyz': No such file or directory
```
