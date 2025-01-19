# Derivation and Deriving Path

We return to derivations and derived paths in the context of making a build system for conventional software.

## [Derivation]{#derivation}

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

 - A set of *outputs*, consisting of names and possibly other data

 - A set of *inputs*, a set of [deriving paths](#deriving-path)

 - Everything needed for an `execve` system call:
   1. Path to executable
   2. A list of arguments (except for `argv[0]`, which is taken from the path in the usual way)
   3. A set of environment variables.

 - A two-component "system" name (e.g. `x86_64-linux`) where the executable is to run.

The path and list/set elements of the other two will presumably consist wholly or partly of store paths.
But just as we stored the references contained in the file data separately for store objects, so we store the set of inputs separately.

The last bit of information is to take advantage of the fact that Nix allows *heterogenous* build plans, were not all steps can be run on the same machine or same sort of machine.

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

The process's job is to produce the outputs, but have no other important side effects.
The rules around this will be discussed in following sections.

### Referencing

Derivations are always referred to by the store path of the store object they are encoded to.
The store path name is the derivation name with `.drv` suffixed at the end.
The store path digest we will explain in a following section after we go over the different variants of derivations, as the exact algorithm depends on them.
Suffice to say for now, it is (a form of) content addressing based on the derivation and its inputs.

> NOTE:
> Actually we have defined "text addressing" already in "content-addressing store objects".
> Note that we reserve the right to format new sorts of derivations on-disk differently in the future.
> The choice of "text hashing" should be deemd arbitrary along with the choice of "ATerm" serialization.
> Maybe this should be moved below to "encoding?".

### Outputs {#outputs}

The outputs are the derivations are the store objects it is obligated to produce.

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

### System {#system}

The system type on which the [`builder`](#attr-builder) executable is meant to be run.

A necessary condition for Nix to build derivations locally is that the `system` attribute matches the current [`system` configuration option].
It can automatically [build on other platforms](@docroot@/language/derivations.md#attr-builder) by forwarding build requests to other machines.

[`system` configuration option]: @docroot@/command-ref/conf-file.md#conf-system


### Builder {#builder}

Path to an executable that will perform the build.

### Args {#args}

Command-line arguments to be passed to the [`builder`](#builder) executable.

### Environment Variables {#env}

Environment variables which will be passed to the [`builder`](#builder) executable.

### Placeholder

TODO

Two types:

- Reference to own outputs

- output derived paths (see below), corresponding to store paths we haven't yet realized.

> N.B. Current method of creating hashes which we substitute for string fields should be seen as an artifact of the current ATerm formula.
> In order to be more explicit, and avoid gotchas analogous to [SQL injection](https://en.wikipedia.org/wiki/SQL_injection),
> we ought to consider switching two a different format where we explicitly use a syntax for a oncatentation of plain strings and placeholders written more explicitly.

### Inputs

The inputs are a set of [deriving paths].
Each of these must be [realised] prior to building the derivation in
question.
At that point, the derivation can be normalized replacing each input
derived path with its store path --- which we now now since we've
realised it.

## [Deriving path]{#deriving-path}

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

[content-addressed derivation]: @docroot@/glossary.md#gloss-content-addressed-derivation
[realise]: @docroot@/glossary.md#gloss-realise
[store object]: @docroot@/store/store-object.md
[store path]: @docroot@/store/store-path.md

## Encoding

### Derivation

There are two formats, documented separately:

- The legacy [ATerm" format](protocols/derivation-aterm.md)

- The modern [JSON format](source/protocols/json/derivation.md)

### Deriving Path

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

**Experimental feature**: [`dynamic-derivations`](@docroot@/contributing/experimental-features.md#xp-feature-dynamic-derivations)

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

### Encoding

The encoding is adjusted in a very simplest way, merely displaying the same

```
/nix/store/lxrn8v5aamkikg6agxwdqd1jz7746wz4-firefox-98.0.2.drv^foo.drv^bar.drv^out
|----------------------------------------------------------------------------| |-|
inner deriving path (usual encoding)                                           output name
|--------------------------------------------------------------------| |-----|
even more inner deriving path (usual encoding)                         output name
|------------------------------------------------------------| |-----|
innermost constant store path (usual encoding)                 output name
```

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
