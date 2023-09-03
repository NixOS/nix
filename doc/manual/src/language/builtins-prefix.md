# Built-in Functions

This section lists the functions built into the Nix language evaluator.
All built-in functions are available through the global [`builtins`](./builtin-constants.md#builtins-builtins) constant.

For convenience, some built-ins can be accessed directly:

- [`derivation`](#builtins-derivation)
- [`import`](#builtins-import)
- [`abort`](#builtins-abort)
- [`throw`](#builtins-throw)

<dl>
  <dt id="builtins-derivation"><a href="#builtins-derivation"><code>derivation <var>attrs</var></code></a></dt>
  <dd><p><var>derivation</var> is described in
         <a href="derivations.md">its own section</a>.</p></dd>
