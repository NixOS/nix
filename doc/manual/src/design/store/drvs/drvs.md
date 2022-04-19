# Derivations

Derivations are recipes to create store objects.

Derivations are the heart of Nix.
Other system (like Git or IPFS) also store and transfer immutable data, but they don't concern themselves with *how* that data was created.
This is where Nix comes in.

Derivations produce data by running arbitrary commands, like Make or Ninja rules.
Unlike those systems, derivations do not produce arbitrary files, but only specific store objects.
They cannot modify the store in any way, other than creating those store objects.
This rigid specification of what they do is what allows Nix's caching to be so simple and yet robust.

Based on the above, we can conceptually break derivations down into 3 parts:

1. What command will be run?

2. What existing store objects are needed as inputs?

3. What store objects will be produced as outputs?

## What command will be run?

The original core of Nix was very simple about this, in the mold of traditional Unix.
Commands consist of 3 parts:

1. Path to executable

2. Arguments (Except for `argv[0]`, which is taken from the path in the usual way)

3. Environment variables.

## What existing store objects are needed as inputs?

The previous sub-section begs the question "how can we be sure the path to the executable points to what we think it does?"
It's a good questions!

## What store objects will be produced as outputs?

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
