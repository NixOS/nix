# Built-ins

This section lists the values and functions built into the Nix language evaluator.
All built-ins are available through the global [`builtins`](#builtins-builtins) constant.

Some built-ins are also exposed directly in the global scope:

- [`derivation`](#builtins-derivation)
- `derivationStrict`
- [`abort`](#builtins-abort)
- [`baseNameOf`](#builtins-baseNameOf)
- [`break`](#builtins-break)
- [`dirOf`](#builtins-dirOf)
- [`false`](#builtins-false)
- [`fetchGit`](#builtins-fetchGit)
- `fetchMercurial`
- [`fetchTarball`](#builtins-fetchTarball)
- [`fetchTree`](#builtins-fetchTree)
- [`fromTOML`](#builtins-fromTOML)
- [`import`](#builtins-import)
- [`isNull`](#builtins-isNull)
- [`map`](#builtins-map)
- [`null`](#builtins-null)
- [`placeholder`](#builtins-placeholder)
- [`removeAttrs`](#builtins-removeAttrs)
- [`scopedImport`](#builtins-scopedImport)
- [`throw`](#builtins-throw)
- [`toString`](#builtins-toString)
- [`true`](#builtins-true)

<dl>
  <dt id="builtins-derivation"><a href="#builtins-derivation"><code>derivation <var>attrs</var></code></a></dt>
  <dd><p><var>derivation</var> is described in
         <a href="derivations.md">its own section</a>.</p></dd>
