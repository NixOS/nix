# Derivation and Derived Path

We return to derivations and derived paths in the context of making a build system for conventional software.

## Derivations

As already discussed, store objects are files (and references), and store references are (encoded as) file paths.
What is natural Unix analog for a build step *in action*?
Answer: a process that will eventually exit, leaving behind some output date.
What is the natural way to *plan* such a step?
An `execve` system call.

Indeed the analogy

> files : (concrete) store objects :: `execve` system calls : (concrete) derivations

is a very straightforward mental trick to remember 90% of what a derivation is.

A derivation consists of:

 - Some bookkeeping:
   - A (base) name
   - A set of outputs, consisting of names and possibly other data
   - A set of inputs, morally a set of derived paths

 - Everything needed for an `execve` system call:
   1. Path to executable
   2. A list of arguments (except for `argv[0]`, which is taken from the path in the usual way)
   3. A set of environment variables.

 - A two component "system" name (e.g. `x86_64-linux`) where the executable is to run.

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

### Output names

Most outputs are named `drv.name + '-' + outputName`.
However, an output named "out" is just has name `drv.name`.
This is to allow derivations with a single output to avoid a superfluous `-<outputName>` in their single output's name when no disambiguation is needed.

### Placeholders

TODO

### Referencing

Derivations are always referred to by the store path of the store object they are encoded to.
The store path name is the derivation name with `.drv` suffixed at the end.
The store path digest we will explain in a following section after we go over the different variants of derivations, as the exact algorithm depends on them.
Suffice to say for now, it is (a form of) content addressing based on the derivation and its inputs.

## Derived paths

Derived references are close to their abstract version, but using `StorePath` as the type of all references, matching the end of the previous subsection.

In pseudo code:

```idris
type OutputName = String

data DerivedPath
  = OpaquePath { path : StorePath }
  | BuiltObj {
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

### Derived Path

Opaque paths are encoded simply as the underlying store path is.
Thus, we see that every encoded store path is also a valid encoded (opaque) derived path.

Built paths are encoded by

- encoding of a store path referring to a derivation

- a separator (`^` or `!` depending on context)

- the name of an output

An example would be:

```
/nix/store/lxrn8v5aamkikg6agxwdqd1jz7746wz4-firefox-98.0.2.drv!out
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ ^^^
```

This parses like so:

```
/nix/store/lxrn8v5aamkikg6agxwdqd1jz7746wz4-firefox-98.0.2.drv!out
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ ^^^
store path (usual encoding)                                    output name
                                                           ^^^
                                                           note the ".drv"
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
