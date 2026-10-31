---
synopsis: "`builtins.pathExists` no longer swallows access errors from evaluating its argument"
prs: 16728
---

Since Nix 2.20, `builtins.pathExists` accidentally recovered from access errors caused by evaluating its argument. Consider:

```sh
$ nix eval --expr 'builtins.pathExists (builtins.readFile /etc/passwd)'
```

This evaluated to `false`, even though `builtins.readFile /etc/passwd` would throw, and `builtins.pathExists` never saw a supposed path.

Now the error propagates, as it did in Nix 2.19 and earlier. This invocation fails with the same error observed by calling `builtins.readFile /etc/passwd` in isolation.
