# Name

`nix-collect-garbage` - delete unreachable [store objects]

# Synopsis

`nix-collect-garbage` [`--delete-old`] [`-d`] [`--delete-older-than` *period*] [`--keep-min` *generations*] [`--keep-max` *generations*] [`--max-freed` *bytes*] [`--dry-run`]

# Description

The command `nix-collect-garbage` is mostly an alias of [`nix-store --gc`](@docroot@/command-ref/nix-store/gc.md).
That is, it deletes all unreachable [store objects] in the Nix store to clean up your system.

However, it provides more additional options,
[`--delete-old`](#opt-delete-old), [`--delete-older-than`](#opt-delete-older-than), [`--keep-min`](#opt-keep-min), and [`--keep-max`](#opt-keep-max),
which also delete old [profiles], allowing potentially more [store objects] to be deleted because profiles are also garbage collection roots.
These options are the equivalent of running
[`nix-env --delete-generations`](@docroot@/command-ref/nix-env/delete-generations.md)
with various augments on multiple profiles,
prior to running `nix-collect-garbage` (or just `nix-store --gc`) without any flags.

> **Note**
>
> Deleting previous configurations makes rollbacks to them impossible.

These flags should be used with care, because they potentially delete generations of profiles used by other users on the system.

## Locations searched for profiles

`nix-collect-garbage` cannot know about all profiles; that information doesn't exist.
Instead, it looks in a few locations, and acts on all profiles it finds there:

1. The default profile locations as specified in the [profiles] section of the manual.

2. > **NOTE**
   >
   > Not stable; subject to change
   >
   > Do not rely on this functionality; it just exists for migration purposes and may change in the future.
   > These deprecated paths remain a private implementation detail of Nix.

   `$NIX_STATE_DIR/profiles` and `$NIX_STATE_DIR/profiles/per-user`.

   With the exception of `$NIX_STATE_DIR/profiles/per-user/root` and `$NIX_STATE_DIR/profiles/default`, these directories are no longer used by other commands.
   `nix-collect-garbage` looks there anyways in order to clean up profiles from older versions of Nix.

# Options

These options are for deleting old [profiles] prior to deleting unreachable [store objects].

- <span id="opt-delete-old">[`--delete-old`](#opt-delete-old)</span> / `-d`

  Delete all old generations of profiles.

  This is the equivalent of invoking [`nix-env --delete-generations old`](@docroot@/command-ref/nix-env/delete-generations.md#generations-old) on each found profile.

- <span id="opt-delete-older-than">[`--delete-older-than`](#opt-delete-older-than)</span> *period*

  Delete all generations of profiles older than the specified amount (except for the generations that were active at that point in time).
  *period* is a value such as `30d`, which would mean 30 days.

  This is the equivalent of invoking [`nix-env --delete-generations <period>`](@docroot@/command-ref/nix-env/delete-generations.md#generations-time) on each found profile.
  See the documentation of that command for additional information about the *period* argument.

- <span id="opt-keep-min">[`--keep-min`](#opt-keep-min)</span> *generations*

  Minimum amount of generations to keep after deletion.

- <span id="opt-keep-max">[`--keep-max`](#opt-keep-max)</span> *generations*

  Maximum amount of generations to keep after deletion.

- <span id="opt-max-freed">[`--max-freed`](#opt-max-freed)</span> *bytes*

<!-- duplication from https://github.com/NixOS/nix/blob/442a2623e48357ff72c77bb11cf2cf06d94d2f90/doc/manual/source/command-ref/nix-store/gc.md?plain=1#L39-L44 -->

  Keep deleting paths until at least *bytes* bytes have been deleted,
  then stop. The argument *bytes* can be followed by the
  multiplicative suffix `K`, `M`, `G` or `T`, denoting KiB, MiB, GiB
  or TiB units.

{{#include ./opt-common.md}}

{{#include ./env-common.md}}

# Examples

## Delete all older

To delete from the Nix store everything that is not used by the current
generations of each profile, do

```console
$ nix-collect-garbage -d
```

## Keep most-recent by time (number of days) and trim by amount

This command will delete generations older than a week if possible, while keeping an amount of generations between `10` and `20`.

```console
$ nix-collect-garbage --delete-older-than 7d --keep-min 10 --keep-max 20
```

If there were more than 20 generations built in the past week, it will only keep 20 most recent ones.
If there were less than 10 generations built in the past week, it will keep even older generations, until there is 10.

[profiles]: @docroot@/command-ref/files/profiles.md
[store objects]: @docroot@/store/store-object.md
