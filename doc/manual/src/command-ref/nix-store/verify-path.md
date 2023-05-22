# Name

`nix-store --verify-path` - check path contents against Nix database

## Synopsis

`nix-store` `--verify-path` *paths…*

## Description

The operation `--verify-path` compares the contents of the given store
paths to their cryptographic hashes stored in Nix’s database. For every
changed path, it prints a warning message. The exit status is 0 if no
path has changed, and 1 otherwise.

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ../env-common.md}}

## Example

To verify the integrity of the `svn` command and all its dependencies:

```console
$ nix-store --verify-path $(nix-store --query --requisites $(which svn))
```

