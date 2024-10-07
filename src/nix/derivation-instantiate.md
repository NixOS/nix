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

  Dump a JSON list of objects containing at least a `drvPath` field with the
  path to the produced store derivation.

# Examples

* Get the store derivation for a single installable, with a gc root

  ```console
    $ nix derivation instantiate github:NixOS/nixpkgs#hello
    /nix/store/af3rc6phyv80h7aq4y3d08awnq2ja8fp-hello-2.12.1.drv
    $ ls -ld drv
    lrwxrwxrwx [...] drv -> /nix/store/af3rc6phyv80h7aq4y3d08awnq2ja8fp-hello-2.12.1.drv
  ```

* Get the store derivations for multiple installables, in the same order as the
  provided arguments.

  ```console
    $ nix derivation instantiate github:NixOS/nixpkgs#{hello,xorg.xclock}
    /nix/store/af3rc6phyv80h7aq4y3d08awnq2ja8fp-hello-2.12.1.drv
    /nix/store/82w6jak6c7zldgvxyq5nwhclz3yp85zp-xclock-1.1.1.drv
  ```

* The same, with JSON output. The values also appear in the same order as CLI parameters.

  ```console
    $ nix derivation instantiate github:NixOS/nixpkgs#{xorg.xclock,hello} --json | jq
    [
      {
        "drvPath": "/nix/store/82w6jak6c7zldgvxyq5nwhclz3yp85zp-xclock-1.1.1.drv"
      },
      {
        "drvPath": "/nix/store/af3rc6phyv80h7aq4y3d08awnq2ja8fp-hello-2.12.1.drv"
      }
    ]
  ```

# Notes

* JSON output format may be extended in the future with other fields.

* Order guarantees will always ensure that the following bash commands output
  the same text.

  ```console
    $ nix derivation instantiate [installables]
    $ nix derivation instantiate [installables] --json | jq ".[] | .drvPath" -r
  ```

)""
