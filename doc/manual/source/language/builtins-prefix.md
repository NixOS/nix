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

<!-- This tip (use `lib` instead) is a "layer violation", but serves an important social role. -->

> **Tip**
>
> **Should I use `builtins` or `lib`?**
>
> The built-ins are designed to be a stable interface that expressions can depend on over time,
> so that, for instance, old Nixpkgs versions continue to evaluate reproducibly.
>
> On the flip side, this means they have accumulated a few quirks that Nix is unable to change,
> but a library like Nixpkgs `lib` *can* improve, replace or deprecate those behaviors,
> because its sources are pinned where reproducibility matters.
>
> So while it is not wrong to use `builtins` directly,
> for instance in small Nixpkgs-independent projects,
> you will have a better experience using a library like `lib` as your primary source of functions,
> as it hides problematic functions, fixes up others,
> and helps you improve your code by means of future deprecations, which are still sufficiently rare.

<dl>
  <dt id="builtins-derivation"><a href="#builtins-derivation"><code>derivation <var>attrs</var></code></a></dt>
  <dd><p><var>derivation</var> is described in
         <a href="derivations.md">its own section</a>.</p></dd>
