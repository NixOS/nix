# Name

`nix-store --gc` - run garbage collection

# Synopsis

`nix-store` `--gc` [`--print-roots` | `--print-live` | `--print-dead`] [`--max-freed` *bytes*]

# Description

Without additional flags, the operation `--gc` performs a garbage
collection on the Nix store. That is, all paths in the Nix store not
reachable via file system references from a set of “roots”, are deleted.

The following suboperations may be specified:

- `--print-roots`

  This operation prints on standard output the set of roots used by
  the garbage collector.

- `--print-live`

  This operation prints on standard output the set of “live” store
  paths, which are all the store paths reachable from the roots. Live
  paths should never be deleted, since that would break consistency —
  it would become possible that applications are installed that
  reference things that are no longer present in the store.

- `--print-dead`

  This operation prints out on standard output the set of “dead” store
  paths, which is just the opposite of the set of live paths: any path
  in the store that is not live (with respect to the roots) is dead.

By default, all unreachable paths are deleted. The following options
control what gets deleted and in what order:

- `--max-freed` *bytes*

  Keep deleting paths until at least *bytes* bytes have been deleted,
  then stop. The argument *bytes* can be followed by the
  multiplicative suffix `K`, `M`, `G` or `T`, denoting KiB, MiB, GiB
  or TiB units.

The behaviour of the collector is also influenced by the
`keep-outputs` and `keep-derivations` settings in the Nix
configuration file.

By default, the collector prints the total number of freed bytes when it
finishes (or when it is interrupted).

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ../env-common.md}}

# Examples

To delete all unreachable paths, just do:

```console
$ nix-store --gc
deleting `/nix/store/kq82idx6g0nyzsp2s14gfsc38npai7lf-cairo-1.0.4.tar.gz.drv'
...
8825586 bytes freed (8.42 MiB)
```

To delete at least 100 MiBs of unreachable paths:

```console
$ nix-store --gc --max-freed $((100 * 1024 * 1024))
```

