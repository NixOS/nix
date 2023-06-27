# Built-in Constants

These constants are built into the Nix language evaluator:

- [`builtins`]{#builtins-builtins} (attribute set)

  Contains all the [built-in functions](./builtins.md) and values, in order to avoid polluting the global scope.

  Since built-in functions were added over time, [testing for attributes](./operators.md#has-attribute) in `builtins` can be used for graceful fallback on older Nix installations:

  ```nix
  # if hasContext is not available, we assume `s` has a context
  if builtins ? hasContext then builtins.hasContext s else true
  ```

- [`builtins.currentSystem`]{#builtins-currentSystem} (string)

  The built-in value `currentSystem` evaluates to the Nix platform
  identifier for the Nix installation on which the expression is being
  evaluated, such as `"i686-linux"` or `"x86_64-darwin"`.

  Not available in [pure evaluation mode](@docroot@/command-ref/conf-file.md#conf-pure-eval).

- [`builtins.currentTime`]{#builtins-currentTime} (integer)

  Return the [Unix time](https://en.wikipedia.org/wiki/Unix_time) at first evaluation.
  Repeated references to that name will re-use the initially obtained value.

  Example:

  ```console
  $ nix repl
  Welcome to Nix 2.15.1 Type :? for help.

  nix-repl> builtins.currentTime
  1683705525

  nix-repl> builtins.currentTime
  1683705525
  ```

  The [store path](@docroot@/glossary.md#gloss-store-path) of a derivation depending on `currentTime` will differ for each evaluation.

  Not available in [pure evaluation mode](@docroot@/command-ref/conf-file.md#conf-pure-eval).
