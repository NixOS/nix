---
synopsis: "Nix now marks the start of infinite recursions"
prs: 8623
---
Nix can now mark the start of an infinite recursion when printing the evaluation stack with `--show-trace`. Look for

```
… entering the infinite recursion
```

This line is not printed if the infinite recursion does not coincide with a trace item.
