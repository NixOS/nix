# Name

`nix-store --load-db` - import Nix database

# Synopsis

`nix-store` `--load-db`

# Description

The operation `--load-db` reads a dump of the Nix database created by
`--dump-db` from standard input and loads it into the Nix database.

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ../env-common.md}}
