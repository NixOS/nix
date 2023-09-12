# Name

`nix-store --dump-db` - export Nix database

# Synopsis

`nix-store` `--dump-db` [*paths…*]

# Description

The operation `--dump-db` writes a dump of the Nix database to standard
output. It can be loaded into an empty Nix store using `--load-db`. This
is useful for making backups and when migrating to different database
schemas.

By default, `--dump-db` will dump the entire Nix database. When one or
more store paths is passed, only the subset of the Nix database for
those store paths is dumped. As with `--export`, the user is responsible
for passing all the store paths for a closure. See `--export` for an
example.

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ../env-common.md}}
