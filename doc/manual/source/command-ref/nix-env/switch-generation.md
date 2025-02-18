# Name

`nix-env --switch-generation` - set user environment to given profile generation

# Synopsis

`nix-env` {`--switch-generation` | `-G`} *generation*

# Description

This operation makes generation number *generation* the current
generation of the active profile. That is, if the `profile` is the path
to the active profile, then the symlink `profile` is made to point to
`profile-generation-link`, which is in turn a symlink to the actual user
environment in the Nix store.

Switching will fail if the specified generation does not exist.

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ./env-common.md}}

{{#include ../env-common.md}}

# Examples

```console
$ nix-env --switch-generation 42
switching from generation 50 to 42
```

