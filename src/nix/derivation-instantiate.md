R""(

# Name

`nix derivation instantiate` - instantiate store derivations

# Synopsis

`nix derivation instantiate`
  [`--out-link` *link prefix*]
  [`--json`]
  [`--no-link`]
  *installables…*

# Description

The command `nix derivation instantiate` produces [store derivation]s from
installables. Each top-level expression should evaluate to a derivation, a list
of derivations, or a set of derivations. The paths of the resulting store
derivations are printed on standard output.

[store derivation]: @docroot@/glossary.md#gloss-store-derivation

# Options

- `--out-link` *link prefix*

  The prefix used for gc roots.

- `--no-link`

  Do not create garbage collector roots for the generated store derivations.

- `--json`

  Dump a JSON object whose keys are the generated store derivations instread of
  printing them directly on the output.

)""
