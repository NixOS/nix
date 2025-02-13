# Store Derivation and Deriving Path

Besides functioning as a [content addressed store] the Nix store layer works as a [build system].
Other system (like Git or IPFS) also store and transfer immutable data, but they don't concern themselves with *how* that data was created.

This is where Nix distinguishes itself.
*Derivations* represent individual build steps, and *deriving paths* are needed to refer to the *outputs* of those build steps before they are built.
<!-- The two concepts need to be introduced together because, as described below, each depends on the other. -->

## Store Derivation {#store-derivation}

A derivation is a specification for running an executable on precisely defined input files to repeatably produce output files at uniquely determined file system paths.

A derivation consists of:

 - A name

 - A set of [*inputs*][inputs], a set of [deriving paths][deriving path]

 - A map of [*outputs*][outputs], from names to other data

 - The ["system" type][system] (e.g. `x86_64-linux`) where the executable is to run.

 - The [process creation fields]: to spawn the arbitrary process which will perform the build step.

[store derivation]: #store-derivation
[inputs]: #inputs
[input]: #inputs
[outputs]: #outputs
[output]: #outputs
[process creation fields]: #process-creation-fields
[builder]: #builder
[args]: #args
[env]: #env
[system]: #system

### Referencing derivations {#derivation-path}

Derivations are always referred to by the [store path] of the store object they are encoded to.
See the [encoding section](#derivation-encoding) for more details on how this encoding works, and thus what exactly what store path we would end up with for a given derivation.

The store path of the store object which encodes a derivation is often called a *derivation path* for brevity.

## Deriving path {#deriving-path}

Deriving paths are a way to refer to [store objects][store object] that may or may not yet be [realised][realise].
There are two forms:

- [*constant*]{#deriving-path-constant}: just a [store path].
  It can be made [valid][validity] by copying it into the store: from the evaluator, command line interface or another store.

- [*output*]{#deriving-path-output}: a pair of a [store path] to a [store derivation] and an [output] name.

In pseudo code:

```typescript
type OutputName = String;

type ConstantPath = {
  path: StorePath;
};

type OutputPath = {
  drvPath: StorePath;
  output: OutputName;
};

type DerivingPath = ConstantPath | OutputPath;
```

Deriving paths are necessary because, in general and particularly for [content-addressing derivations][content-addressing derivation], the [store path] of an [output] is not known in advance.
We can use an output deriving path to refer to such an out, instead of the store path which we do not yet know.

[deriving path]: #deriving-path
[validity]: @docroot@/glossary.md#gloss-validity

## Parts of a derivation

A derivation is constructed from the parts documented in the following subsections.

### Inputs {#inputs}

The inputs are a set of [deriving paths][deriving path], refering to all store objects needed in order to perform this build step.

The [process creation fields] will presumably include many [store paths][store path]:

 - The path to the executable normally starts with a store path
 - The arguments and environment variables likely contain many other store paths.

But rather than somehow scanning all the other fields for inputs, Nix requires that all inputs be explicitly collected in the inputs field. It is instead the responsibility of the creator of a derivation (e.g. the evaluator) to  ensure that every store object referenced in another field (e.g. referenced by store path) is included in this inputs field.

### Outputs {#outputs}

The outputs are the derivations are the [store objects][store object] it is obligated to produce.

Outputs are assigned names, and also consistent of other information based on the type of derivation.

Output names can be any string which is also a valid [store path] name.
The store path of the output store object (also called an [output path] for short), has a name based on the derivation name and the output name.
In the general case, store paths have name `derivationName + "-" + outputName`.
However, an output named "out" has a store path with name is just the derivation name.
This is to allow derivations with a single output to avoid a superfluous `"-${outputName}"` in their single output's name when no disambiguation is needed.

> **Example**
>
> A derivation is named `hello`, and has two outputs, `out`, and `dev`
>
> - The derivation's path will be: `/nix/store/<hash>-hello.drv`.
>
> - The store path of `out` will be: `/nix/store/<hash>-hello`.
>
> - The store path of `dev` will be: `/nix/store/<hash>-hello-dev`.

### System {#system}

The system type on which the [`builder`](#attr-builder) executable is meant to be run.

A necessary condition for Nix to schedule a given derivation on some Nix instance is for the "system" of that derivation to match that instance's [`system` configuration option].

By putting the `system` in each derivation, Nix allows *heterogenous* build plans, where not all steps can be run on the same machine or same sort of machine.
Nix can schedule builds such that it automatically builds on other platforms by [forwarding build requests](@docroot@/advanced-topics/distributed-builds.md) to other Nix instances.

[`system` configuration option]: @docroot@/command-ref/conf-file.md#conf-system

[content-addressing derivation]: @docroot@/glossary.md#gloss-content-addressing-derivation
[realise]: @docroot@/glossary.md#gloss-realise
[store object]: @docroot@/store/store-object.md
[store path]: @docroot@/store/store-path.md

### Process creation fields {#process-creation-fields}

These are the three fields which describe how to spawn the process which (along with any of its own child processes) will perform the build.
You may note that this has everything needed for an `execve` system call.

#### Builder {#builder}

This is the path to an executable that will perform the build and produce the [outputs].

#### Arguments {#args}

Command-line arguments to be passed to the [`builder`](#builder) executable.

Note that these are the arguments after the first argument.
The first argument passed to the `builder` will be the value of `builder`, as per the usual convention on Unix.
See [Wikipedia](https://en.wikipedia.org/wiki/Argv) for details.

#### Environment Variables {#env}

Environment variables which will be passed to the [builder](#builder) executable.

### Placeholders

Placeholders are opaque values used within the [process creation fields] to [store objects] for which we don't yet know [store path]s.
They are strings in the form `/<hash>` that are embedded anywhere within the strings of those fields, and we are [considering](https://github.com/NixOS/nix/issues/12361) to add store-path-like placeholders.

> **Note**
>
> Output Deriving Path exist to solve the same problem as placeholders --- that is, referring to store objects for which we don't yet know a store path.
> They also have a string syntax with `^`, [described in the encoding section](#deriving-path-encoding).
> We could use that syntax instead of `/<hash>` for placeholders, but its human-legibility would cause problems.

There are two types of placeholder, corresponding to the two cases where this problem arises:

- [Output placeholder]{#output-placeholder}:

  This is a placeholder for a derivation's own output.

- [Input placeholder]{#input-placeholder}:

  This is a placeholder to a derivation's non-constant [input],
  i.e. an input that is an [output derived path].

> **Explanation**
>
> In general, we need to realise [realise] a [store object] in order to be sure to have a store object for it.
> But for these two cases this is either impossible or impractical:
>
> - In the output case this is impossible:
>
>   We cannot build the output until we have a correct derivation, and we cannot have a correct derivation (without using placeholders) until we have the output path.
>
> - In the input case this is impractical:
>
>   If we always build a dependency first, and then refer to its output by store path, we would lose the ability for a derivation graph to describe an entire build plan consisting of multiple build steps.

## Encoding

### Derivation {#derivation-encoding}

There are two formats, documented separately:

- The legacy ["ATerm" format](@docroot@/protocols/derivation-aterm.md)

- The experimental, currently under development and changing [JSON format](@docroot@/protocols/json/derivation.md)

Every derivation has a canonical choice of encoding used to serialize it to a store object.
This ensures that there is a canonical [store path] used to refer to the derivation, as described in [Referencing derivations](#derivation-path).

> **Note**
>
> Currently, the canonical encoding for every derivation is the "ATerm" format,
> but this is subject to change for types derivations which are not yet stable.

Regardless of the format used, when serializing a derivation to a store object, that store object will be content-addressed.

In the common case, the inputs to store objects are either:

 - [constant deriving paths](#deriving-path-constant) for content-addressed source objects, which are "initial inputs" rather than the outputs of some other derivation

 - the outputs of other derivations

If those other derivations *also* abide by this common case (and likewise for transitive inputs), then the entire closure of the serialized derivation will be content-addressed.

### Deriving Path {#deriving-path-encoding}

- *constant*

  Constant deriving paths are encoded simply as the underlying store path is.
  Thus, we see that every encoded store path is also a valid encoded (constant) deriving path.

- *output*

  Output deriving paths are encoded by

  - encoding of a store path referring to a derivation

  - a `^` separator (or `!` in some legacy contexts)

  - the name of an output of the previously referred derivation

  > **Example**
  >
  > ```
  > /nix/store/lxrn8v5aamkikg6agxwdqd1jz7746wz4-firefox-98.0.2.drv^out
  > ```
  >
  > This parses like so:
  >
  > ```
  > /nix/store/lxrn8v5aamkikg6agxwdqd1jz7746wz4-firefox-98.0.2.drv^out
  > |------------------------------------------------------------| |-|
  > store path (usual encoding)                                    output name
  >                                                           |--|
  >                                                           note the ".drv"
  > ```

## Extending the model to be higher-order

**Experimental feature**: [`dynamic-derivations`](@docroot@/development/experimental-features.md#xp-feature-dynamic-derivations)

So far, we have used store paths to refer to derivations.
That works because we've implicitly assumed that all derivations are created *statically* --- created by some mechanism out of band, and then manually inserted into the store.
But what if derivations could also be created dynamically within Nix?
In other words, what if derivations could be the outputs of other derivations?

:::{.note}
In the parlance of "Build Systems à la carte", we are generalizing the Nix store layer to be a "Monadic" instead of "Applicative" build system.
:::

How should we refer to such derivations?
A deriving path works, the same as how we refer to other derivation outputs.
But what about a dynamic derivations output?
(i.e. how do we refer to the output of an output of a derivation?)
For that we need to generalize the definition of deriving path, replacing the store path used to refer to the derivation with a nested deriving path:

```diff
 type OutputPath = {
-  drvPath: StorePath;
+  drvPath: DerivingPath;
   output: OutputName;
 };
```

Now, the `drvPath` field of `OutputPath` is itself a `DerivingPath` instead of a `StorePath`.

With that change, here is updated definition:

```typescript
type OutputName = String;

type ConstantPath = {
  path: StorePath;
};

type OutputPath = {
  drvPath: DerivingPath;
  output: OutputName;
};

type DerivingPath = ConstantPath | OutputPath;
```

Under this extended model, `DerivingPath`s are thus inductively built up from a root `ConstantPath`, wrapped with zero or more outer `OutputPath`s.

### Encoding {#deriving-path-encoding}

The encoding is adjusted in the natural way, encoding the `drv` field recursively using the same deriving path encoding.
The result of this is that it is possible to have a chain of `^<output-name>` at the end of the final string, as opposed to just a single one.

> **Example**
>
> ```
> /nix/store/lxrn8v5aamkikg6agxwdqd1jz7746wz4-firefox-98.0.2.drv^foo.drv^bar.drv^out
> |----------------------------------------------------------------------------| |-|
> inner deriving path (usual encoding)                                           output name
> |--------------------------------------------------------------------| |-----|
> even more inner deriving path (usual encoding)                         output name
> |------------------------------------------------------------| |-----|
> innermost constant store path (usual encoding)                 output name
> ```
