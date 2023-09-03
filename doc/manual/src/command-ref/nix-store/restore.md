# Name

`nix-store --restore` - extract a Nix archive

## Synopsis

`nix-store` `--restore` *path*

## Description

The operation `--restore` unpacks a NAR archive to *path*, which must
not already exist. The archive is read from standard input.

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ../env-common.md}}
