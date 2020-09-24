Title: nix-collect-garbage

# Name

`nix-collect-garbage` - delete unreachable store paths

# Synopsis

`nix-collect-garbage` [`--delete-old`] [`-d`] [`--delete-older-than` *period*] [`--max-freed` *bytes*] [`--dry-run`]

# Description

The command `nix-collect-garbage` is mostly an alias of [`nix-store
--gc`](nix-store.md#operation---gc), that is, it deletes all
unreachable paths in the Nix store to clean up your system. However,
it provides two additional options: `-d` (`--delete-old`), which
deletes all old generations of all profiles in `/nix/var/nix/profiles`
by invoking `nix-env --delete-generations old` on all profiles (of
course, this makes rollbacks to previous configurations impossible);
and `--delete-older-than` *period*, where period is a value such as
`30d`, which deletes all generations older than the specified number
of days in all profiles in `/nix/var/nix/profiles` (except for the
generations that were active at that point in time).

# Example

To delete from the Nix store everything that is not used by the current
generations of each profile, do

```console
$ nix-collect-garbage -d
```
