# Name

`nix-store --restore` - extract a Nix archive

## Synopsis

`nix-store` `--restore` *path*

## Description

The operation `--restore` unpacks a [Nix Archive (NAR)][Nix Archive] to *path*, which must
not already exist. The archive is read from standard input.

[Nix Archive]: @docroot@/store/file-system-object/content-address.md#serial-nix-archive

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ../env-common.md}}
