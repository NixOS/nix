# Nix Language

The Nix language is

- *domain-specific*

  It only exists for the Nix package manager:
  to describe packages and configurations as well as their variants and compositions.
  It is not intended for general purpose use.

- *declarative*

  There is no notion of executing sequential steps.
  Dependencies between operations are established only through data.

- *pure*

  Values cannot change during computation.
  Functions always produce the same output if their input does not change.

- *functional*

  Functions are like any other value.
  Functions can be assigned to names, taken as arguments, or returned by functions.

- *lazy*

  Expressions are only evaluated when their value is needed.

- *dynamically typed*

  Type errors are only detected when expressions are evaluated.

