# Name

`nix-env --switch-profile` - set user environment to given profile

# Synopsis

`nix-env` {`--switch-profile` | `-S`} *path*

# Description

This operation makes *path* the current profile for the user. That is,
the symlink `~/.nix-profile` is made to point to *path*.

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ./env-common.md}}

{{#include ../env-common.md}}

# Examples

```console
$ nix-env --switch-profile ~/my-profile
```
