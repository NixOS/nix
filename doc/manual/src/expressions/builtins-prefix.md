# Built-in Functions

This section lists the functions built into the Nix expression
evaluator. (The built-in function `derivation` is discussed above.)
Some built-ins, such as `derivation`, are always in scope of every Nix
expression; you can just access them right away. But to prevent
polluting the namespace too much, most built-ins are not in
scope. Instead, you can access them through the `builtins` built-in
value, which is a set that contains all built-in functions and values.
For instance, `derivation` is also available as `builtins.derivation`.

  - `derivation` *attrs*; `builtins.derivation` *attrs*  

    `derivation` is described in [its own section](derivations.md).

