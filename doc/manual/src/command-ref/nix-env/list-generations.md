# Name

`nix-env --list-generations` - list profile generations

# Synopsis

`nix-env` `--list-generations`

# Description

This operation print a list of all the currently existing generations
for the active profile. These may be switched to using the
`--switch-generation` operation. It also prints the creation date of the
generation, and indicates the current generation.

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ./env-common.md}}

{{#include ../env-common.md}}

# Examples

```console
$ nix-env --list-generations
  95   2004-02-06 11:48:24
  96   2004-02-06 11:49:01
  97   2004-02-06 16:22:45
  98   2004-02-06 16:24:33   (current)
```

