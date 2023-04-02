# Name

`nix-env --delete-generations` - delete profile generations

# Synopsis

`nix-env` `--delete-generations` *generations*

# Description

This operation deletes the specified generations of the current profile.
The generations can be a list of generation numbers, the special value
`old` to delete all non-current generations, a value such as `30d` to
delete all generations older than the specified number of days (except
for the generation that was active at that point in time), or a value
such as `+5` to keep the last `5` generations ignoring any newer than
current, e.g., if `30` is the current generation `+5` will delete
generation `25` and all older generations. Periodically deleting old
generations is important to make garbage collection effective.

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ./env-common.md}}

{{#include ../env-common.md}}

# Examples

```console
$ nix-env --delete-generations 3 4 8
```

```console
$ nix-env --delete-generations +5
```

```console
$ nix-env --delete-generations 30d
```

```console
$ nix-env -p other_profile --delete-generations old
```

