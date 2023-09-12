# Name

`nix-env --uninstall` - remove packages from user environment

# Synopsis

`nix-env` {`--uninstall` | `-e`} *drvnames…*

# Description

The uninstall operation creates a new user environment, based on the
current generation of the active profile, from which the store paths
designated by the symbolic names *drvnames* are removed.

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ./env-common.md}}

{{#include ../env-common.md}}

# Examples

```console
$ nix-env --uninstall gcc
$ nix-env --uninstall '.*' (remove everything)
```
