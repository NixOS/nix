---
synopsis: "`nix-repl`'s `:doc` shows documentation comments"
significance: significant
issues:
- 3904
- 10771
prs:
- 1652
- 9054
- 11072
---

`nix repl` has a `:doc` command that previously only rendered documentation for internally defined functions.
This feature has been extended to also render function documentation comments, in accordance with [RFC 145].

Example:

```
nix-repl> :doc lib.toFunction
Function toFunction
    … defined at /home/user/h/nixpkgs/lib/trivial.nix:1072:5

    Turns any non-callable values into constant functions. Returns
    callable values as is.

Inputs

    v

      : Any value

Examples

    :::{.example}

## lib.trivial.toFunction usage example

      | nix-repl> lib.toFunction 1 2
      | 1
      | 
      | nix-repl> lib.toFunction (x: x + 1) 2
      | 3

    :::
```

Known limitations:
- It does not render documentation for "formals", such as `{ /** the value to return */ x, ... }: x`.
- Some extensions to markdown are not yet supported, as you can see in the example above.

We'd like to acknowledge [Yingchi Long (@inclyc)](https://github.com/inclyc) for proposing a proof of concept for this functionality in [#9054](https://github.com/NixOS/nix/pull/9054), as well as [@sternenseemann](https://github.com/sternenseemann) and [Johannes Kirschbauer (@hsjobeki)](https://github.com/hsjobeki) for their contributions, proposals, and their work on [RFC 145].

Author: [**Robert Hensing (@roberth)**](https://github.com/roberth)

[RFC 145]: https://github.com/NixOS/rfcs/pull/145
