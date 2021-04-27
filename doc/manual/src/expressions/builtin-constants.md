# Built-in Constants

Here are the constants built into the Nix expression evaluator:

  - `builtins`  
    The set `builtins` contains all the built-in functions and values.
    You can use `builtins` to test for the availability of features in
    the Nix installation, e.g.,
    
    ```nix
    if builtins ? getEnv then builtins.getEnv "PATH" else ""
    ```
    
    This allows a Nix expression to fall back gracefully on older Nix
    installations that don’t have the desired built-in function.

  - `builtins.currentSystem`  
    The built-in value `currentSystem` evaluates to the Nix platform
    identifier for the Nix installation on which the expression is being
    evaluated, such as `"i686-linux"` or `"x86_64-darwin"`.
