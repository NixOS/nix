# Built-ins

This section lists the values and functions built into the Nix language evaluator.
All built-ins are available through the global [`builtins`](#builtins-builtins) constant.

Some built-ins are also exposed directly in the global scope:

<!-- TODO(@rhendric, #10970): this list is incomplete -->

- [`derivation`](#builtins-derivation)
- [`import`](#builtins-import)
- [`abort`](#builtins-abort)
- [`throw`](#builtins-throw)

<dl>
  <dt id="builtins-derivation"><a href="#builtins-derivation"><code>derivation <var>attrs</var></code></a></dt>
  <dd><p><var>derivation</var> is described in
         <a href="derivations.md">its own section</a>.</p></dd>
