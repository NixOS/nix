# Name

`nix-env --delete-generations` - delete profile generations

# Synopsis

`nix-env` `--delete-generations` *generations*

# Description

This operation deletes the specified generations of the current profile.

*generations* can be a one of the following:

- <span id="generations-list">[`<number>...`](#generations-list)</span>

  A list of generation numbers, each one a separate command-line argument.

  Delete exactly the profile generations given by their generation number.
  Deleting the current generation is not allowed.

- <span id="generations-old">[The special value `old`](#generations-old)</span>

  Delete all generations except the current one.

  > **WARNING**
  >
  > Older *and newer* generations will be deleted by this operation.
  >
  > One might expect this to just delete older generations than the current one, but that is only true if the current generation is also the latest.
  > Because one can roll back to a previous generation, it is possible to have generations newer than the current one.
  > They will also be deleted.

- <span id="generations-time">[`<number>d`](#generations-time)</span>

  The last *number* days

  *Example*: `30d`

  Delete all generations created more than *number* days ago, except the most recent one of them.
  This allows rolling back to generations that were available within the specified period.

- <span id="generations-count">[`+<number>`](#generations-count)</span>

  The last *number* generations up to the present

  *Example*: `+5`

  Keep the last *number* generations, along with any newer than current.

Periodically deleting old generations is important to make garbage collection
effective.
The is because profiles are also garbage collection roots — any [store object] reachable from a profile is "alive" and ineligible for deletion.

[store object]: @docroot@/store/store-object.md

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ./env-common.md}}

{{#include ../env-common.md}}

# Examples

## Delete explicit generation numbers

```console
$ nix-env --delete-generations 3 4 8
```

Delete the generations numbered 3, 4, and 8, so long as the current active generation is not any of those.

## Keep most-recent by count (number of generations)

```console
$ nix-env --delete-generations +5
```

Suppose `30` is the current generation, and we currently have generations numbered `20` through `32`.

Then this command will delete generations `20` through `25` (`<= 30 - 5`),
and keep generations `26` through `31` (`> 30 - 5`).

## Keep most-recent by time (number of days)

```console
$ nix-env --delete-generations 30d
```

This command will delete all generations older than 30 days, except for the generation that was active 30 days ago (if it currently exists).

## Delete all older

```console
$ nix-env --profile other_profile --delete-generations old
```
