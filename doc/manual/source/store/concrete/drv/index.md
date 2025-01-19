# Derivation and Deriving Path

We return to derivations and derived paths in the context of making a build system for conventional software.

## Derivation {#derivation}

A derivation is a specification for running an executable on precisely defined input files to repeatably produce output files at uniquely determined file system paths.

As already discussed, store objects are files (and references), and store references are (encoded as) file paths.
What is natural Unix analog for a build step *in action*?
Answer: a process that will eventually exit, leaving behind some output date.
What is the natural way to *plan* such a step?
An `execve` system call.

Indeed the analogy

> files : (concrete) store objects :: `execve` system calls : (concrete) derivations

is a very straightforward mental trick to remember 90% of what a derivation is.

A derivation consists of:

 - A (base) name

 - A set of [*inputs*][inputs], a set of [deriving paths][deriving path]

 - A map of [*outputs*][outputs], from names to other data

 - Everything needed for an `execve` system call:

   - The ["builder"][builder], a path to an executable

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
[outputs]: #outputs
[inputs]: #inputs
[builder]: #builder
[args]: #args
[env]: #env
[system]: #system

### Parts of a derivation

#### Inputs {#inputs}

The inputs are a set of [deriving paths][deriving path], refering to all store objects needed in order to perform this build step.

The information needed for the `execve` system call will presumably include many [store paths][store path]:

 - The path to the executable is almost surely starts with a store path
 - The arguments and environment variables likely contain many other store paths.

But just as we stored the references contained in the file data separately for store objects, so we store the set of inputs separately from the builder, arguments, and environment variables.

#### Outputs {#outputs}

The outputs are the derivations are the [store objects][store object] it is obligated to produce.

Outputs are assigned names, and also consistent of other information based on the type of derivation.

Output names can be any string which is also a valid [store path] name.
The store path of the output store object (also called an [output path] for short), has a name based on the derivation name and the output name.
Most outputs are named `drvMame + '-' + outputName`.
However, an output named "out" is just has name `drvName`.
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

#### Builder {#builder}

This is the path to an executable that will perform the build and produce the [outputs].

#### Arguments {#args}

Command-line arguments to be passed to the [`builder`](#builder) executable.

Note that these are the arguments after the first argument.
The first argument, `argv[0]`, is the "base name" of the `builder`, as per the usual convention on Unix.
See [Wikipedia](https://en.wikipedia.org/w/index.php?title=Argv) for details.

#### Environment Variables {#env}

Environment variables which will be passed to the [builder](#builder) executable.

#### System {#system}

The system type on which the [`builder`](#attr-builder) executable is meant to be run.

A necessary condition for Nix to schedule a given derivation on given Nix instance is for the "system" of that derivation to match that instance's [`system` configuration option].

By putting the `system` in each derivation, Nix allows *heterogenous* build plans, where not all steps can be run on the same machine or same sort of machine.
A Nix isntance scheduling builds can automatically [build on other platforms](@docroot@/language/derivations.md#attr-builder) by forwarding build requests to other Nix instances.

[`system` configuration option]: @docroot@/command-ref/conf-file.md#conf-system

### Referencing derivations

Derivations are always referred to by the [store path] of the store object they are encoded to.
See the [encoding section](#derivation-encoding) for more details on how this encoding works, and thus what exactly what store path we would end up with for a given derivations.

The store path of the store object which encodes a derivation is often called a "derivation path" for brevity.

### Placeholder

TODO

Two types:

- Reference to own outputs

- output derived paths (see below), corresponding to store paths we haven't yet realized.

> N.B. Current method of creating hashes which we substitute for string fields should be seen as an artifact of the current "ATerm" serialization format.
> In order to be more explicit, and avoid gotchas analogous to [SQL injection](https://en.wikipedia.org/wiki/SQL_injection),
> we ought to consider switching two a different format where we explicitly use a syntax for a oncatentation of plain strings and placeholders written more explicitly.

## Deriving path {#deriving-path}

Deriving paths are close to their abstract version, but using `StorePath` as the type of all references, matching the end of the previous subsection.

Deriving paths are a way to refer to [store objects][store object] that might not yet be [realised][realise].
This is necessary because, in general and particularly for [content-addressed derivations][content-addressed derivation], the [store path] of an [output] is not known in advance.
There are two forms:

- *constant*: just a [store path]
  It can be made [valid][validity] by copying it into the store: from the evaluator, command line interface or another st    ore.

- *output*: a pair of a [store path] to a [derivation] and an [output] name.

In pseudo code:

```idris
type OutputName = String

data DerivingPath
  = ConstantPath { path : StorePath }
  | Output {
      drvPath : StorePath,
      output  : OutputName,
    }
```

[deriving path]: #deriving-path

[content-addressed derivation]: @docroot@/glossary.md#gloss-content-addressed-derivation
[realise]: @docroot@/glossary.md#gloss-realise
[store object]: @docroot@/store/store-object.md
[store path]: @docroot@/store/store-path.md

## Encoding

### Derivation {#derivation-encoding}

There are two formats, documented separately:

- The legacy ["ATerm" format](@docroot@/protocols/derivation-aterm.md)

- The modern [JSON format](@docroot@/protocols/json/derivation.md)

Currently derivations are always serialized to store objects using the "ATerm" format, but this is subject to change.

Regardless of the format used, when serializing to store object, content-addressing is always used.
In the common case the inputs to store objects are either:

 - constant deriving paths for content-addressed source objects, which are "initial inputs" rather than the outputs of some other derivation (except in the case of bootstrap binaries).

 - the outputs of other derivations abiding by this same invariant.

This common case makes for the following useful property:
when we serialize such a derivation graph to store objects, the resulting closures are *entirely* content-addressed.

Here is a sketch at the proof of this:

 - The inputs which are constant deriving paths become references of the serialized derivations, but they are content-addressed per the above.

 - For inputs which are output deriving paths, we cannot directly reference the input because in general it is not built yet.
   We instead "peal back" the output deriving path to take its underlying serialized derivation (the `drvPath` field), and reference that.
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

  - a separator (`^` or `!` depending on context)

  - the name of an output

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
  = ConstantPath { storeObj : StorePath }
  | Output {
      drv    : DerivingPath, -- Note: changed
      output : OutputName,
    }
```

Now, the `drv` field of `Output` is itself a `DerivingPath` instead of an `StorePath`.

Under this extended model, `DerivingPath`s are thus inductively built up from an `ConstantPath`, contains in 0 or more outer `Output`s.

### Encoding {#deriving-path-encoding}

The encoding is adjusted in a very simplest way, merely displaying the same

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

## Extra extensions

### `__structuredAttrs`

Historically speaking, most users of Nix made GNU Bash with a script the command run, regardless of what they were doing.
Bash variable are automatically created from env vars, but bash also supports array and string-keyed map variables in addition to string variables.
People also usually create derivations using language which also support these richer data types.
It was thus desired a way to get this data from the language "planning" the derivation to language to bash, the language evaluated at "run time".

`__structuredAttrs` does this by smuggling inside the core derivation format a map of named richer data.
At run time, this becomes two things:

1. A JSON file containing that map.
2. A bash script setting those variables.

The bash command can be passed a script which will "source" that Nix-created bash script, setting those variables with the richer data.
The outer script can then do whatever it likes with those richer variables as input.

However, since derivations can already contain arbitary input sources, the vast majority of `__structuredAttrs` can be handled by upper layers.
We might consider implementing `__structuredAttrs` in higher layers in the future, and simplifying the store layer.
