# String context

> **Note**
>
> This is an advanced topic.
> The Nix language is designed to be used without the programmer consciously dealing with string contexts or even knowing what they are.

A string in the Nix language is not just a sequence of characters like strings in other languages.
It is actually a pair of a sequence of characters and a *string context*.
The string context is an (unordered) set of *string context elements*.

The purpose of string contexts is to collect non-string values attached to strings via
[string concatenation](./operators.md#string-concatenation),
[string interpolation](./string-interpolation.md),
and similar operations.
The idea is that a user can combine together values to create a build recipe without manually keeping track of where the "ingredients" come from, and then the Nix language does that bookkeeping implicitly to come up with the right derivation inputs.

> In line with this goal, string contexts are *not* explicitly manipulated in idiomatic Nix code.
> Strings with non-empty contexts are only concatenated and eventually passed to `builtins.derivation`.
> Plain strings with empty contexts are the only ones to be inspected, e.g. using comparison with `==`.

String context elements come in different forms:

- [*constant*]{#string-context-element-constant}
- [*output*]{#string-context-element-output}
- [*derivation deep*]{#string-context-element-derivation-deep}

*Constant* and *output* string contexts elements are just
[deriving paths](@docroot@/glossary.md#gloss-deriving-path);
those are just the names of the two kinds of deriving path.
See the documentation on deriving paths for further details.

*derivation deep* is an advanced feature intended to be used with the
[`exportReferencesGraph`](./advanced-attributes.html#adv-attr-exportReferencesGraph)
advanced derivation feature.
A *derivation deep* string context element is a derivation path, and refers to both its outputs and the entire build closure of that derivation:
all its outputs, all the other derivations the given derivation depends on, and all their outputs too.

## Inspecting string contexts

Most basically, [`builtins.hasContext`] will tell whether a string has a non-empty context.

When more granular information is needed, [`builtins.getContext`] can be used.
It creates an [attribute set] representing the string context, which can be inspected as usual.

[`builtins.hasContext`]: ./builtins.md#builtins-hasContext
[`builtins.getContext`]: ./builtins.md#builtins-getContext
[attribute set]: ./values.md#attribute-set

## Clearing string contexts

[`buitins.unsafeDiscardStringContext`](./builtins.md#) will make a copy of a string, but with an empty string context.
The returned string can be used in more ways, e.g. by operators that require the string context to be empty.
The requirement to explicitly discard the string context in such use cases helps ensure that string context elements are not lost by mistake.
The "unsafe" marker is only there to remind that Nix normally guarantees that dependencies are tracked, whereas the returned string has lost them.

## Constructing string contexts

[`builtins.appendContext`] will create a copy of a string, but with additional string context elements.
The context is specified explicitly by an [attribute set] in the format that [`builtins.hasContext`] produces.
A string with arbitrary contexts can be made like this:

1. Create a string with the desired string context elements.
   (The contents of the string do not matter.)
2. Dump its context with [`builtins.getContext`].
3. Combine it with a base string and repeated [`builtins.appendContext`] calls.

The remainder of this section will focus on step 1: making strings with individual string context elements on which to apply `builtins.getContext`.

[`builtins.appendContext`]: ./builtins.md#builtins-appendContext

### Constant string context elements

A constant string context element is just a constant [deriving path];
a constant deriving path is just a [store path].
We therefore want to use [`builtins.storePath`] to create a string with a single constant string context element:

> **Example**
>
> ```nix
> builtins.getContext (builtins.storePath "/nix/store/wkhdf9jinag5750mqlax6z2zbwhqb76n-hello-2.10")
> ```
> evaluates to
> ```nix
> {
>   "/nix/store/wkhdf9jinag5750mqlax6z2zbwhqb76n-hello-2.10" = {
>     path = true;
>   };
> }
> ```

[deriving path]: @docroot@/glossary.md#gloss-deriving-path
[store path]: @docroot@/glossary.md#gloss-store-path
[`builtins.storePath`]: ./builtins.md#builtins-storePath

### Output string context elements

> **Example**
>
> This is best illustrated with a built-in function that is still experimental: [`builtins.ouputOf`].
> This example will *not* work the stable Nix!
>
> ```nix
> builtins.getContext
>   (builtins.outputOf
>     (builtins.storePath "/nix/store/fvchh9cvcr7kdla6n860hshchsba305w-hello-2.12.drv")
>     "out")
> ```
> evaluates to
> ```nix
> {
>   "/nix/store/fvchh9cvcr7kdla6n860hshchsba305w-hello-2.12.drv" = {
>     outputs = [ "out" ];
>   };
> }
> ```

[`builtins.outputOf`]: ./builtins.md#builtins-outputOf

### "Derivation deep" string context elements

The best way to illustrate this is with [`builtins.addDrvOutputDependencies`].
We take a regular constant string context element pointing to a derivation, and transform it into a "Derivation deep" string context element.

> **Example**
>
> ```nix
> builtins.getContext
>   (builtins.addDrvOutputDependencies
>     (builtins.storePath "/nix/store/fvchh9cvcr7kdla6n860hshchsba305w-hello-2.12.drv"))
> ```
> evaluates to
> ```nix
> {
>   "/nix/store/fvchh9cvcr7kdla6n860hshchsba305w-hello-2.12.drv" = {
>     allOutputs = true;
>   };
> }
> ```

[`builtins.addDrvOutputDependencies`]: ./builtins.md#builtins-addDrvOutputDependencies
[`builtins.unsafeDiscardOutputDependency`]: ./builtins.md#builtins-unsafeDiscardOutputDependency
