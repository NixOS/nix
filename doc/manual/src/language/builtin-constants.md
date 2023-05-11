# Built-in Constants

These constants are built into the Nix language evaluator:

- [`builtins`]{#builtins-builtins} (attribute set)

  Contains all the [built-in functions](./builtins.md) and values, in order to avoid polluting the global scope.

  Since built-in functions were added over time, [testing for attributes](./operators.md#has-attribute) in `builtins` can be used for graceful fallback on older Nix installations:

  ```nix
  if builtins ? getEnv then builtins.getEnv "PATH" else ""
  ```

- [`builtins.currentSystem`]{#builtins-currentSystem} (string)

  The built-in value `currentSystem` evaluates to the Nix platform
  identifier for the Nix installation on which the expression is being
  evaluated, such as `"i686-linux"` or `"x86_64-darwin"`.
