---
synopsis: "Add `pipe-operators` experimental feature"
prs:
- 11131
---

This is a draft implementation of [RFC 0148](https://github.com/NixOS/rfcs/pull/148).

The `pipe-operators` experimental feature adds [`<|` and `|>` operators][pipe operators] to the Nix language.
*a* `|>` *b* is equivalent to the function application *b* *a*, and
*a* `<|` *b* is equivalent to the function application *a* *b*.

For example:

```
nix-repl> 1 |> builtins.add 2 |> builtins.mul 3
9

nix-repl> builtins.add 1 <| builtins.mul 2 <| 3
7
```

`<|` and `|>` are right and left associative, respectively, and have lower precedence than any other operator.
These properties may change in future releases.

See [the RFC](https://github.com/NixOS/rfcs/pull/148) for more examples and rationale.

[pipe operators]: @docroot@/language/operators.md#pipe-operators
