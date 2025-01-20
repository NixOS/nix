# Derivation and Deriving Path

We return to derivations and derived paths in the context of making a build system for conventional software.

## Derivation {#derivation}

A derivation is a specification for running an executable on precisely defined input files to repeatably produce output files at uniquely determined file system paths.

As already discussed, store objects are files (and references), and store references are (encoded as) file paths.
What is the natural Unix analog for a build step *in action*?
Answer: a process that will eventually exit, leaving behind some output files.
What is the natural way to *plan* such a step?
An `execve` system call.

Indeed the analogy

> files : (concrete) store objects :: `execve` system calls : (concrete) derivations

is a very straightforward mental trick to remember 90% of what a derivation is.

A derivation consists of:

 - A name

 - A set of [*inputs*][inputs], a set of [deriving paths][deriving path]

 - A map of [*outputs*][outputs], from names to other data

 - The [process creation fields]: to spawn the arbitrary process which will perform the build step:


   - The [*builder*][builder], a path to an executable

   - A list of [arguments][args]

   - A map of [environment variables][env].

 - A two-component ["system" type][system] (e.g. `x86_64-linux`) where the executable is to run.

All together in pseudo-code:

```idris
data DerivedRef -- from below

type OutputName = String

-- to be discussed, depends on exact type of derivation
data DerivationOutput

record Derivation where
  outputs : Map OutputName DerivationOutput
  inputs  : Set DerivedPath
  builder : Path
  args    : List String
  env     : Map String String
  name    : String

inputs drv = drv.inputs

outputs drv = Map.keys drv.outputs
```

[derivation]: #derivation
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

Deriving paths are close to their abstract version, but using `StorePath` as the type of all references, matching the end of the previous subsection.

Deriving paths are a way to refer to [store objects][store object] that might not yet be [realised][realise].
This is necessary because, in general and particularly for [content-addressed derivations][content-addressed derivation], the [store path] of an [output] is not known in advance.
There are two forms:

- [*constant*]{#deriving-path-constant}: just a [store path].
  It can be made [valid][validity] by copying it into the store: from the evaluator, command line interface or another st    ore.

- [*output*]{#deriving-path-output}: a pair of a [store path] to a [derivation] and an [output] name.

In pseudo code:

```idris
type OutputName = String

data DerivingPath
  = Constant { path : StorePath }
  | Output {
      drvPath : StorePath,
      output  : OutputName,
    }
```

[deriving path]: #deriving-path
[validity]: @docroot@/glossary.md#gloss-validity

## Parts of a derivation

With both [derivations][derivation] introduced and [deriving paths][deriving path] defined,
it is now possible to define the parts of a derivation.

### Inputs {#inputs}

The inputs are a set of [deriving paths][deriving path], refering to all store objects needed in order to perform this build step.

The information needed for the `execve` system call will presumably include many [store paths][store path]:

 - The path to the executable is almost surely starts with a store path
 - The arguments and environment variables likely contain many other store paths.

But just as we stored the references contained in the file data separately for store objects, so we store the set of inputs separately from the builder, arguments, and environment variables.

### Outputs {#outputs}

The outputs are the derivations are the [store objects][store object] it is obligated to produce.

Outputs are assigned names, and also consistent of other information based on the type of derivation.

Output names can be any string which is also a valid [store path] name.
The store path of the output store object (also called an [output path] for short), has a name based on the derivation name and the output name.
Most outputs' store paths have name `drvMame + '-' + outputName`.
However, an output named "out" has a store path with name `drvName`.
This is to allow derivations with a single output to avoid a superfluous `-<outputName>` in their single output's name when no disambiguation is needed.

> **Example**
>
> A derivation is named `hello`, and has two outputs, `out`, and `dev`
>
> - The derivation's path will be: `/nix/store/<hash>-hello.drv`.
>
> - The store path of `out` will be: `/nix/store/<hash>-hello`.
>
> - The store path of `dev` will be: `/nix/store/<hash>-hello-dev`.

### Process creation fields {#process-creation-fields}

These are the three fields which describe out to spawn the process which (along with any of its own child processes) will perform the build.
As state in the derivation introduction, this is everything needed for an `execve` system call.

#### Builder {#builder}

This is the path to an executable that will perform the build and produce the [outputs].

#### Arguments {#args}

Command-line arguments to be passed to the [`builder`](#builder) executable.

Note that these are the arguments after the first argument.
The first argument, `argv[0]`, is the "base name" of the `builder`, as per the usual convention on Unix.
See [Wikipedia](https://en.wikipedia.org/w/index.php?title=Argv) for details.

#### Environment Variables {#env}

Environment variables which will be passed to the [builder](#builder) executable.

#### Placeholders

Placeholders are opaque values used within the [process creation fields] to [store objects] for which we don't yet know [store path]s.
The are strings in the form `/<hash>` that are embedded anywhere within the strings of those fields.

> **Note**
>
> Output Deriving Path exist to solve the same problem as placeholders --- that is, referring to store objects for which we don't yet know a store path.
> They also have a string syntax, [descibed in the encoding section](#deriving-path-encoding).
> We could use that syntax instead of `/<hash>` for placeholders, but its human-legibility would cuse problems.

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
>   We cannot built the output until we have a correct derivation, and we cannot have a correct derivation (without using placeholders) until we have the output path.
>
> - In the input case this is impractical:
>
>   We an always built a dependency, and then refer to its output by store path, but by doing so we loose the ability for a derivation graph to describe an entire build plan consisting of multiple build steps.

> **Note**
>
> The current method of creating hashes which we substitute for string fields should be seen as an artifact of the current "ATerm" serialization format.
> In order to be more explicit, and avoid gotchas analogous to [SQL injection](https://en.wikipedia.org/wiki/SQL_injection),
> we ought to consider switching two a different format where we explicitly use a syntax for the concatenation of plain strings and [deriving paths] written more explicitly.

### System {#system}

The system type on which the [`builder`](#attr-builder) executable is meant to be run.

A necessary condition for Nix to schedule a given derivation on given Nix instance is for the "system" of that derivation to match that instance's [`system` configuration option].

By putting the `system` in each derivation, Nix allows *heterogenous* build plans, where not all steps can be run on the same machine or same sort of machine.
A Nix isntance scheduling builds can automatically [build on other platforms](@docroot@/language/derivations.md#attr-builder) by forwarding build requests to other Nix instances.

[`system` configuration option]: @docroot@/command-ref/conf-file.md#conf-system

[content-addressed derivation]: @docroot@/glossary.md#gloss-content-addressed-derivation
[realise]: @docroot@/glossary.md#gloss-realise
[store object]: @docroot@/store/store-object.md
[store path]: @docroot@/store/store-path.md

## Encoding

### Derivation {#derivation-encoding}

There are two formats, documented separately:

- The legacy ["ATerm" format](@docroot@/protocols/derivation-aterm.md)

- The experimental [JSON format](@docroot@/protocols/json/derivation.md)

Every derivation has a canonical choice of encoding used to serialize it to a store object.
This ensures that there is a canonical [store path] used to refer to the derivation, as described in [Referencing derivations](#derivation-path).

> **Note**
>
> Currently, the canonical encoding for every derivation is the "ATerm" format,
> but this is subject to change for types derivations which are not yet stable.

Regardless of the format used, when serializing to store objects, content-addressing is always used.

In the common case the inputs to store objects are either:

 - constant deriving paths for content-addressed source objects, which are "initial inputs" rather than the outputs of some other derivation

 - the outputs of other derivations abiding by this same invariant.

This common case makes for the following useful property:
when we serialize such a derivation graph to store objects, the resulting closures are *entirely* content-addressed.

Here is a sketch at the proof of this:

 - The inputs which are constant deriving paths become references of the serialized derivations, but they are content-addressed per the above.

 - For inputs which are output deriving paths, we cannot directly reference the input because in general it is not built yet.
   We instead "peel back" the output deriving path to take its underlying serialized derivation (the `drvPath` field), and reference that.
   Since it is a derivation, it must be content-addressed

 - There are no other ways a store object would end up in an input closure.
   The references of a derivation in store object form always come from solely from the inputs of the derivation.

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

We can apply the same extension discussed for the abstract model to the concrete model.
Again, only the data type for Deriving Paths needs to be modified.
Derivations are the same except for using the new extended deriving path data type.

```idris
type OutputName = String

data DerivingPath
  = Constant { storeObj : StorePath }
  | Output {
      drv    : DerivingPath, -- Note: changed
      output : OutputName,
    }
```

Now, the `drv` field of `Output` is itself a `DerivingPath` instead of a `StorePath`.

Under this extended model, `DerivingPath`s are thus inductively built up from an `Constant`, contains in 0 or more outer `Output`s.

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
