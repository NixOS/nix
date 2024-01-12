---
synopsis: Some stack overflow segfaults are fixed
issues: 9616
prs: 9617
---

The number of nested function calls has been restricted, to detect and report
infinite function call recursions. The default maximum call depth is 10,000 and
can be set with [the `max-call-depth`
option](@docroot@/command-ref/conf-file.md#conf-max-call-depth).

This fixes segfaults or the following unhelpful error message in many cases:

    error: stack overflow (possible infinite recursion)

Before:

```
$ nix-instantiate --eval --expr '(x: x x) (x: x x)'
Segmentation fault: 11
```

After:

```
$ nix-instantiate --eval --expr '(x: x x) (x: x x)'
error: stack overflow

       at «string»:1:14:
            1| (x: x x) (x: x x)
             |              ^
```
