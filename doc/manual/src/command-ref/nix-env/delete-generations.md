# Name

`nix-env --delete-generations` - delete profile generations

# Synopsis

`nix-env` `--delete-generations` *generations*

# Description

This operation deletes the specified generations of the current profile.

*generations* can be a one of the following:

- <span id="generations-list">`<number>...`</span>:\
  A list of generation numbers, each one a separate command-line argument.

  Delete exactly the profile generations given by their generation number.
  Deleting the current generation is not allowed.

- The special value <span id="generations-old">`old`</span>

  Delete all generations older than the current one.

- <span id="generations-days">`<days>d`</span>:\
  The last *days* days

  *Example*: `30d`

  Delete all generations older than *days* days.
  The generation that was active at that point in time is excluded, and will not be deleted.

- <span id="generations-count">`+<count>`</span>:\
  The last *count* generations up to the present

  *Example*: `+5`

  Keep the last *count* generations, along with any newer than current.

Periodically deleting old generations is important to make garbage collection
effective.
The is because profiles are also garbage collection roots — any [store object] reachable from a profile is "alive" and ineligible for deletion.

[store object]: @docroot@/glossary.md#gloss-store-object

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

## Keep most-recent by count count

```console
$ nix-env --delete-generations +5
```

Suppose `30` is the current generation, and we currently have generations numbered `20` through `32`.

Then this command will delete generations `20` through `25` (`<= 30 - 5`),
and keep generations `26` through `31` (`> 30 - 5`).

## Keep most-recent in days

```console
$ nix-env --delete-generations 30d
```

This command will delete all generations older than 30 days, except for the generation that was active 30 days ago (if it currently exists).

## Delete all older

```console
$ nix-env --profile other_profile --delete-generations old
```
