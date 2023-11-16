# Derivation and Deriving Path

So far, we have covered "inert" store objects.
But the point of the Nix store layer is to be a build system.
Other system (like Git or IPFS) also store and transfer immutable data, but they don't concern themselves with *how* that data was created.

This is where Nix distinguishes itself.
*Derivations* represent individual build steps, and *deriving paths* are needed to to the *outputs* of those build steps.
The two concepts need to be introduced together because, as described below, each depends on the other.

## Derivation

What is natural Unix analog for a build step *in action*?
Answer: a process that will eventually exit, leaving behind some output date.
What is the natural way to *plan* such a step?
An `execve` system call.

A derivation consists of:

 - A (base) name

 - A set of *outputs*, consisting of names and possibly other data

 - A set of *inputs*, a set of deriving paths

 - Everything needed for an `execve` system call:
   1. Path to executable
   2. A list of arguments (except for `argv[0]`, which is taken from the path in the usual way)
   3. A set of environment variables.

 - A two-component "system" name (e.g. `x86_64-linux`) where the executable is to run.

The path and list/set elements of the other two will presumably consist wholly or partly of store paths.
But just as we stored the references contained in the file data separately for store objects, so we store the set of inputs separately.

The last bit of information is to take advantage of the fact that Nix allows *heterogenous* build plans, were not all steps can be run on the same machine or same sort of machine.

The process's job is to produce the outputs, but have no other important side effects.
The rules around this will be discussed in following sections.

### Output name

Most outputs are named `drv.name + '-' + outputName`.
However, an output named "out" is just has name `drv.name`.
This is to allow derivations with a single output to avoid a superfluous `-<outputName>` in their single output's name when no disambiguation is needed.

### Placeholder

TODO

### Referencing

Derivations are always referred to by the store path of the store object they are encoded to.
The store path name is the derivation name with `.drv` suffixed at the end.
The store path digest we will explain in a following section after we go over the different variants of derivations, as the exact algorithm depends on them.
Suffice to say for now, it is (a form of) content addressing based on the derivation and its inputs.

## Deriving path

Deriving references are close to their abstract version, but using `StorePath` as the type of all references, matching the end of the previous subsection.

In pseudo code:

```idris
type OutputName = String

data DerivingPath
  = ConstantPath { path : StorePath }
  | Output {
      drv    : StorePath,
      output : OutputName,
    }
```

## Encoding

### Derivation

- The name is not encoded, because we can just get it from the store object!

:::{.note}
Brief amusing history of PP-ATerm
:::

#### `inputSrcs` vs `inputDrvs`

### Deriving Path

Constant deriving paths are encoded simply as the underlying store path is.
Thus, we see that every encoded store path is also a valid encoded (constant) deriving path.

Output deriving paths are encoded by

- encoding of a store path referring to a derivation

- a separator (`^` or `!` depending on context)

- the name of an output

An example would be:

```
/nix/store/lxrn8v5aamkikg6agxwdqd1jz7746wz4-firefox-98.0.2.drv^out
```

This parses like so:

```
/nix/store/lxrn8v5aamkikg6agxwdqd1jz7746wz4-firefox-98.0.2.drv^out
|------------------------------------------------------------| |-|
store path (usual encoding)                                    output name
                                                          |--|
                                                          note the ".drv"
```

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
      drv    : DerivingPath, -- changed
      output : OutputName,
    }
```

Now, the `drv` field of `BuiltObject` is itself a `DerivingPath` instead of an `StorePath`.

Under this extended model, `DerivingPath`s are thus inductively built up from an `ConstantPath`, contains in 0 or more outer `Outputs`.

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
