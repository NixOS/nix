# Name

`nix-env --switch-profile` - set user environment to given profile

# Synopsis

`nix-env` {`--switch-profile` | `-S`} *path*

# Description

This operation makes *path* the current profile for the user.
That is, the symlink `~/.nix-profile` is made to point to *path*.

If the profile doesn’t exist, it will be created automatically.

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ./env-common.md}}

{{#include ../env-common.md}}

# Examples

```console
$ nix-env --switch-profile /nix/var/nix/profiles/my-profile

$ nix-env --switch-profile /nix/var/nix/profiles/default

$ nix-env --switch-profile ~/my-profile
```
