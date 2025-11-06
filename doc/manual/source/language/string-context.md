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
The idea is that a user can reference other files when creating text files through Nix expressions, without manually keeping track of the exact paths.
Nix will ensure that the all referenced files are accessible – that all [store paths](@docroot@/glossary.md#gloss-store-path) are [valid](@docroot@/glossary.md#gloss-validity).

> **Note**
>
> String contexts are *not* explicitly manipulated in idiomatic Nix language code.

String context elements come in different forms:

- [deriving path]{#string-context-element-derived-path}

  A string context element of this type is a [deriving path](@docroot@/glossary.md#gloss-deriving-path).
  They can be either of type [constant](#string-context-constant) or [output](#string-context-output), which correspond to the types of deriving paths.

  - [Constant string context elements]{#string-context-constant}

    > **Example**
    >
    > [`builtins.storePath`] creates a string with a single constant string context element:
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

  - [Output string context elements]{#string-context-output}

    > **Example**
    >
    > The behavior of string contexts are best demonstrated with a built-in function that is still experimental: [`builtins.outputOf`].
    > This example will *not* work with stable Nix!
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

- [*derivation deep*]{#string-context-element-derivation-deep}

  *derivation deep* is an advanced feature intended to be used with the
  [`exportReferencesGraph` derivation attribute](./advanced-attributes.html#adv-attr-exportReferencesGraph).
  A *derivation deep* string context element is a derivation path, and refers to both its outputs and the entire build closure of that derivation:
  all its outputs, all the other derivations the given derivation depends on, and all the outputs of those.

  > **Example**
  >
  > The best way to illustrate *derivation deep* string contexts is with [`builtins.addDrvOutputDependencies`].
  > Take a regular constant string context element pointing to a derivation, and transform it into a "Derivation deep" string context element.
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

## Inspecting string contexts

Most basically, [`builtins.hasContext`] will tell whether a string has a non-empty context.

When more granular information is needed, [`builtins.getContext`] can be used.
It creates an [attribute set] representing the string context, which can be inspected as usual.

[`builtins.hasContext`]: ./builtins.md#builtins-hasContext
[`builtins.getContext`]: ./builtins.md#builtins-getContext
[attribute set]: ./types.md#type-attrs

## Clearing string contexts

[`builtins.unsafeDiscardStringContext`](./builtins.md#builtins-unsafeDiscardStringContext) will make a copy of a string, but with an empty string context.
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

[`builtins.appendContext`]: ./builtins.md#builtins-appendContext
