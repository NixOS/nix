# Name

`nix-env --set` - set profile to contain a specified derivation

## Synopsis

`nix-env` `--set` *drvname*

## Description

The `--set` operation modifies the current generation of a profile so
that it contains exactly the specified derivation, and nothing else.

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ./env-common.md}}

{{#include ../env-common.md}}

## Examples

The following updates a profile such that its current generation will
contain just Firefox:

```console
$ nix-env --profile /nix/var/nix/profiles/browser --set firefox
```

