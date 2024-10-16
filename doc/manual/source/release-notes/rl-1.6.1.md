# Release 1.6.1 (2013-10-28)

This is primarily a bug fix release. Changes of interest are:

  - Nix 1.6 accidentally changed the semantics of antiquoted paths in
    strings, such as `"${/foo}/bar"`. This release reverts to the Nix
    1.5.3 behaviour.

  - Previously, Nix optimised expressions such as `"${expr}"` to *expr*.
    Thus it neither checked whether *expr* could be coerced to a string,
    nor applied such coercions. This meant that `"${123}"` evaluatued to
    `123`, and `"${./foo}"` evaluated to `./foo` (even though `"${./foo}
    "` evaluates to `"/nix/store/hash-foo "`). Nix now checks the type
    of antiquoted expressions and applies coercions.

  - Nix now shows the exact position of undefined variables. In
    particular, undefined variable errors in a `with` previously didn't
    show *any* position information, so this makes it a lot easier to
    fix such errors.

  - Undefined variables are now treated consistently. Previously, the
    `tryEval` function would catch undefined variables inside a `with`
    but not outside. Now `tryEval` never catches undefined variables.

  - Bash completion in `nix-shell` now works correctly.

  - Stack traces are less verbose: they no longer show calls to builtin
    functions and only show a single line for each derivation on the
    call stack.

  - New built-in function: `builtins.typeOf`, which returns the type of
    its argument as a string.
