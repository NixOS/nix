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

  - `import` *path*; `builtins.import` *path*  

    Load, parse and return the Nix expression in the file *path*. If
    *path* is a directory, the file ` default.nix ` in that directory
    is loaded. Evaluation aborts if the file doesn’t exist or contains
    an incorrect Nix expression. `import` implements Nix’s module
    system: you can put any Nix expression (such as a set or a
    function) in a separate file, and use it from Nix expressions in
    other files.

    > **Note**
    >
    > Unlike some languages, `import` is a regular function in Nix.
    > Paths using the angle bracket syntax (e.g., `import` *\<foo\>*)
    > are [normal path values](language-values.md).

    A Nix expression loaded by `import` must not contain any *free
    variables* (identifiers that are not defined in the Nix expression
    itself and are not built-in). Therefore, it cannot refer to
    variables that are in scope at the call site. For instance, if you
    have a calling expression

    ```nix
    rec {
      x = 123;
      y = import ./foo.nix;
    }
    ```

    then the following `foo.nix` will give an error:

    ```nix
    x + 456
    ```

    since `x` is not in scope in `foo.nix`. If you want `x` to be
    available in `foo.nix`, you should pass it as a function argument:

    ```nix
    rec {
      x = 123;
      y = import ./foo.nix x;
    }
    ```

    and

    ```nix
    x: x + 456
    ```

    (The function argument doesn’t have to be called `x` in `foo.nix`;
    any name would work.)
