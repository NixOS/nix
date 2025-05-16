# Name

`nix-store --query` - display information about store paths

# Synopsis

`nix-store` {`--query` | `-q`}
  {`--outputs` | `--requisites` | `-R` | `--references` | `--referrers` |
  `--referrers-closure` | `--deriver` | `-d` | `--valid-derivers` |
  `--graph` | `--tree` | `--binding` *name* | `-b` *name* | `--hash` |
  `--size` | `--roots`}
  [`--use-output`] [`-u`] [`--force-realise`] [`-f`]
  *paths…*

# Description

The operation `--query` displays various bits of information about the
store paths . The queries are described below. At most one query can be
specified. The default query is `--outputs`.

The paths *paths* may also be symlinks from outside of the Nix store, to
the Nix store. In that case, the query is applied to the target of the
symlink.

# Common query options

- `--use-output` / `-u`

  For each argument to the query that is a [store derivation], apply the
  query to the output path of the derivation instead.

- `--force-realise` / `-f`

  Realise each argument to the query first (see [`nix-store --realise`](./realise.md)).

[store derivation]: @docroot@/glossary.md#gloss-store-derivation

# Queries

- `--outputs`

  Prints out the [output paths] of the store
  derivations *paths*. These are the paths that will be produced when
  the derivation is built.

  [output paths]: @docroot@/glossary.md#gloss-output-path

- `--references`

  Prints the set of [references] of the store paths
  *paths*, that is, their immediate dependencies. (For *all*
  dependencies, use `--requisites`.)

  [references]: @docroot@/glossary.md#gloss-reference

- `--requisites` / `-R`

  Prints out the set of [*requisites*][requisite] (better known as the [closure]) of the store path *paths*.

  [requisite]: @docroot@/glossary.md#gloss-requisite
  [closure]: @docroot@/glossary.md#gloss-closure

  This query has one option:

    - `--include-outputs`
      Also include the existing output paths of [store derivation]s,
      and their closures.

  This query can be used to implement various kinds of deployment. A
  *source deployment* is obtained by distributing the closure of a
  store derivation. A *binary deployment* is obtained by distributing
  the closure of an output path. A *cache deployment* (combined
  source/binary deployment, including binaries of build-time-only
  dependencies) is obtained by distributing the closure of a store
  derivation and specifying the option `--include-outputs`.

- `--referrers`

  Prints the set of [*referrers*][referrer] of the store paths *paths*, that is,
  the store paths currently existing in the Nix store that refer to
  one of *paths*. Note that contrary to the references, the set of
  referrers is not constant; it can change as store paths are added or
  removed.

  [referrer]: @docroot@/glossary.md#gloss-referrer

- `--referrers-closure`

  Prints the closure of the set of store paths *paths* under the
  [referrers relation][referrer]; that is, all store paths that directly or
  indirectly refer to one of *paths*. These are all the path currently
  in the Nix store that are dependent on *paths*.

  [referrer]: @docroot@/glossary.md#gloss-referrer

- `--deriver` / `-d`

  Prints the [deriver] that was used to build the store paths *paths*. If
  the path has no deriver (e.g., if it is a source file), or if the
  deriver is not known (e.g., in the case of a binary-only
  deployment), the string `unknown-deriver` is printed.
  The returned deriver is not guaranteed to exist in the local store, for
  example when *paths* were substituted from a binary cache.
  Use `--valid-derivers` instead to obtain valid paths only.

  [deriver]: @docroot@/glossary.md#gloss-deriver

- `--valid-derivers`

  Prints a set of derivation files (`.drv`) which are supposed produce
  said paths when realized. Might print nothing, for example for source paths
  or paths substituted from a binary cache.

- `--graph`

  Prints the references graph of the store paths *paths* in the format
  of the `dot` tool of AT\&T's [Graphviz
  package](http://www.graphviz.org/). This can be used to visualise
  dependency graphs. To obtain a build-time dependency graph, apply
  this to a store derivation. To obtain a runtime dependency graph,
  apply it to an output path.

- `--tree`

  Prints the references graph of the store paths *paths* as a nested
  ASCII tree. References are ordered by descending closure size; this
  tends to flatten the tree, making it more readable. The query only
  recurses into a store path when it is first encountered; this
  prevents a blowup of the tree representation of the graph.

- `--graphml`

  Prints the references graph of the store paths *paths* in the
  [GraphML](http://graphml.graphdrawing.org/) file format. This can be
  used to visualise dependency graphs. To obtain a build-time
  dependency graph, apply this to a [store derivation]. To obtain a
  runtime dependency graph, apply it to an output path.

- `--binding` *name* / `-b` *name*

  Prints the value of the attribute *name* (i.e., environment
  variable) of the [store derivation]s *paths*. It is an error for a
  derivation to not have the specified attribute.

- `--hash`

  Prints the SHA-256 hash of the contents of the store paths *paths*
  (that is, the hash of the output of `nix-store --dump` on the given
  paths). Since the hash is stored in the Nix database, this is a fast
  operation.

- `--size`

  Prints the size in bytes of the contents of the store paths *paths*
  — to be precise, the size of the output of `nix-store --dump` on
  the given paths. Note that the actual disk space required by the
  store paths may be higher, especially on filesystems with large
  cluster sizes.

- `--roots`

  Prints the garbage collector roots that point, directly or
  indirectly, at the store paths *paths*.

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ../env-common.md}}

# Examples

Print the closure (runtime dependencies) of the `svn` program in the
current user environment:

```console
$ nix-store --query --requisites $(which svn)
/nix/store/5mbglq5ldqld8sj57273aljwkfvj22mc-subversion-1.1.4
/nix/store/9lz9yc6zgmc0vlqmn2ipcpkjlmbi51vv-glibc-2.3.4
...
```

Print the build-time dependencies of `svn`:

```console
$ nix-store --query --requisites $(nix-store --query --deriver $(which svn))
/nix/store/02iizgn86m42q905rddvg4ja975bk2i4-grep-2.5.1.tar.bz2.drv
/nix/store/07a2bzxmzwz5hp58nf03pahrv2ygwgs3-gcc-wrapper.sh
/nix/store/0ma7c9wsbaxahwwl04gbw3fcd806ski4-glibc-2.3.4.drv
... lots of other paths ...
```

The difference with the previous example is that we ask the closure of
the derivation (`-qd`), not the closure of the output path that contains
`svn`.

Show the build-time dependencies as a tree:

```console
$ nix-store --query --tree $(nix-store --query --deriver $(which svn))
/nix/store/7i5082kfb6yjbqdbiwdhhza0am2xvh6c-subversion-1.1.4.drv
+---/nix/store/d8afh10z72n8l1cr5w42366abiblgn54-builder.sh
+---/nix/store/fmzxmpjx2lh849ph0l36snfj9zdibw67-bash-3.0.drv
|   +---/nix/store/570hmhmx3v57605cqg9yfvvyh0nnb8k8-bash
|   +---/nix/store/p3srsbd8dx44v2pg6nbnszab5mcwx03v-builder.sh
...
```

Show all paths that depend on the same OpenSSL library as `svn`:

```console
$ nix-store --query --referrers $(nix-store --query --binding openssl $(nix-store --query --deriver $(which svn)))
/nix/store/23ny9l9wixx21632y2wi4p585qhva1q8-sylpheed-1.0.0
/nix/store/5mbglq5ldqld8sj57273aljwkfvj22mc-subversion-1.1.4
/nix/store/dpmvp969yhdqs7lm2r1a3gng7pyq6vy4-subversion-1.1.3
/nix/store/l51240xqsgg8a7yrbqdx1rfzyv6l26fx-lynx-2.8.5
```

Show all paths that directly or indirectly depend on the Glibc (C
library) used by `svn`:

```console
$ nix-store --query --referrers-closure $(ldd $(which svn) | grep /libc.so | awk '{print $3}')
/nix/store/034a6h4vpz9kds5r6kzb9lhh81mscw43-libgnomeprintui-2.8.2
/nix/store/15l3yi0d45prm7a82pcrknxdh6nzmxza-gawk-3.1.4
...
```

Note that `ldd` is a command that prints out the dynamic libraries used
by an ELF executable.

Make a picture of the runtime dependency graph of the current user
environment:

```console
$ nix-store --query --graph ~/.nix-profile | dot -Tps > graph.ps
$ gv graph.ps
```

Show every garbage collector root that points to a store path that
depends on `svn`:

```console
$ nix-store --query --roots $(which svn)
/nix/var/nix/profiles/default-81-link
/nix/var/nix/profiles/default-82-link
/home/eelco/.local/state/nix/profiles/profile-97-link
```
