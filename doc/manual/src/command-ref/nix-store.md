# Name

`nix-store` - manipulate or query the Nix store

# Synopsis

`nix-store` *operation* [*options…*] [*arguments…*]
  [`--option` *name* *value*]
  [`--add-root` *path*]

# Description

The command `nix-store` performs primitive operations on the Nix store.
You generally do not need to run this command manually.

`nix-store` takes exactly one *operation* flag which indicates the
subcommand to be performed. These are documented below.

# Common options

This section lists the options that are common to all operations. These
options are allowed for every subcommand, though they may not always
have an effect.

  - `--add-root` *path*\
    Causes the result of a realisation (`--realise` and
    `--force-realise`) to be registered as a root of the garbage
    collector. *path* will be created as a symlink to the resulting
    store path. In addition, a uniquely named symlink to *path* will
    be created in `/nix/var/nix/gcroots/auto/`. For instance,

    ```console
    $ nix-store --add-root /home/eelco/bla/result -r ...

    $ ls -l /nix/var/nix/gcroots/auto
    lrwxrwxrwx    1 ... 2005-03-13 21:10 dn54lcypm8f8... -> /home/eelco/bla/result

    $ ls -l /home/eelco/bla/result
    lrwxrwxrwx    1 ... 2005-03-13 21:10 /home/eelco/bla/result -> /nix/store/1r11343n6qd4...-f-spot-0.0.10
    ```

    Thus, when `/home/eelco/bla/result` is removed, the GC root in the
    `auto` directory becomes a dangling symlink and will be ignored by
    the collector.

    > **Warning**
    >
    > Note that it is not possible to move or rename GC roots, since
    > the symlink in the `auto` directory will still point to the old
    > location.

    If there are multiple results, then multiple symlinks will be
    created by sequentially numbering symlinks beyond the first one
    (e.g., `foo`, `foo-2`, `foo-3`, and so on).

# Operation `--realise`

## Synopsis

`nix-store` {`--realise` | `-r`} *paths…* [`--dry-run`]

## Description

The operation `--realise` essentially “builds” the specified store
paths. Realisation is a somewhat overloaded term:

  - If the store path is a *derivation*, realisation ensures that the
    output paths of the derivation are [valid](../glossary.md) (i.e.,
    the output path and its closure exist in the file system). This
    can be done in several ways. First, it is possible that the
    outputs are already valid, in which case we are done
    immediately. Otherwise, there may be [substitutes](../glossary.md)
    that produce the outputs (e.g., by downloading them). Finally, the
    outputs can be produced by performing the build action described
    by the derivation.

  - If the store path is not a derivation, realisation ensures that the
    specified path is valid (i.e., it and its closure exist in the file
    system). If the path is already valid, we are done immediately.
    Otherwise, the path and any missing paths in its closure may be
    produced through substitutes. If there are no (successful)
    substitutes, realisation fails.

The output path of each derivation is printed on standard output. (For
non-derivations argument, the argument itself is printed.)

The following flags are available:

  - `--dry-run`\
    Print on standard error a description of what packages would be
    built or downloaded, without actually performing the operation.

  - `--ignore-unknown`\
    If a non-derivation path does not have a substitute, then silently
    ignore it.

  - `--check`\
    This option allows you to check whether a derivation is
    deterministic. It rebuilds the specified derivation and checks
    whether the result is bitwise-identical with the existing outputs,
    printing an error if that’s not the case. The outputs of the
    specified derivation must already exist. When used with `-K`, if an
    output path is not identical to the corresponding output from the
    previous build, the new output path is left in
    `/nix/store/name.check.`

    See also the `build-repeat` configuration option, which repeats a
    derivation a number of times and prevents its outputs from being
    registered as “valid” in the Nix store unless they are identical.

Special exit codes:

  - `100`\
    Generic build failure, the builder process returned with a non-zero
    exit code.

  - `101`\
    Build timeout, the build was aborted because it did not complete
    within the specified `timeout`.

  - `102`\
    Hash mismatch, the build output was rejected because it does not
    match the [`outputHash` attribute of the
    derivation](../expressions/advanced-attributes.md).

  - `104`\
    Not deterministic, the build succeeded in check mode but the
    resulting output is not binary reproducible.

With the `--keep-going` flag it's possible for multiple failures to
occur, in this case the 1xx status codes are or combined using binary
or.

    1100100
       ^^^^
       |||`- timeout
       ||`-- output hash mismatch
       |`--- build failure
       `---- not deterministic

## Examples

This operation is typically used to build store derivations produced by
[`nix-instantiate`](nix-instantiate.md):

```console
$ nix-store -r $(nix-instantiate ./test.nix)
/nix/store/31axcgrlbfsxzmfff1gyj1bf62hvkby2-aterm-2.3.1
```

This is essentially what [`nix-build`](nix-build.md) does.

To test whether a previously-built derivation is deterministic:

```console
$ nix-build '<nixpkgs>' -A hello --check -K
```

# Operation `--serve`

## Synopsis

`nix-store` `--serve` [`--write`]

## Description

The operation `--serve` provides access to the Nix store over stdin and
stdout, and is intended to be used as a means of providing Nix store
access to a restricted ssh user.

The following flags are available:

  - `--write`\
    Allow the connected client to request the realization of
    derivations. In effect, this can be used to make the host act as a
    remote builder.

## Examples

To turn a host into a build server, the `authorized_keys` file can be
used to provide build access to a given SSH public key:

```console
$ cat <<EOF >>/root/.ssh/authorized_keys
command="nice -n20 nix-store --serve --write" ssh-rsa AAAAB3NzaC1yc2EAAAA...
EOF
```

# Operation `--gc`

## Synopsis

`nix-store` `--gc` [`--print-roots` | `--print-live` | `--print-dead`] [`--max-freed` *bytes*]

## Description

Without additional flags, the operation `--gc` performs a garbage
collection on the Nix store. That is, all paths in the Nix store not
reachable via file system references from a set of “roots”, are deleted.

The following suboperations may be specified:

  - `--print-roots`\
    This operation prints on standard output the set of roots used by
    the garbage collector.

  - `--print-live`\
    This operation prints on standard output the set of “live” store
    paths, which are all the store paths reachable from the roots. Live
    paths should never be deleted, since that would break consistency —
    it would become possible that applications are installed that
    reference things that are no longer present in the store.

  - `--print-dead`\
    This operation prints out on standard output the set of “dead” store
    paths, which is just the opposite of the set of live paths: any path
    in the store that is not live (with respect to the roots) is dead.

By default, all unreachable paths are deleted. The following options
control what gets deleted and in what order:

  - `--max-freed` *bytes*\
    Keep deleting paths until at least *bytes* bytes have been deleted,
    then stop. The argument *bytes* can be followed by the
    multiplicative suffix `K`, `M`, `G` or `T`, denoting KiB, MiB, GiB
    or TiB units.

The behaviour of the collector is also influenced by the
`keep-outputs` and `keep-derivations` settings in the Nix
configuration file.

By default, the collector prints the total number of freed bytes when it
finishes (or when it is interrupted). With `--print-dead`, it prints the
number of bytes that would be freed.

## Examples

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

# Operation `--delete`

## Synopsis

`nix-store` `--delete` [`--ignore-liveness`] *paths…*

## Description

The operation `--delete` deletes the store paths *paths* from the Nix
store, but only if it is safe to do so; that is, when the path is not
reachable from a root of the garbage collector. This means that you can
only delete paths that would also be deleted by `nix-store --gc`. Thus,
`--delete` is a more targeted version of `--gc`.

With the option `--ignore-liveness`, reachability from the roots is
ignored. However, the path still won’t be deleted if there are other
paths in the store that refer to it (i.e., depend on it).

## Example

```console
$ nix-store --delete /nix/store/zq0h41l75vlb4z45kzgjjmsjxvcv1qk7-mesa-6.4
0 bytes freed (0.00 MiB)
error: cannot delete path `/nix/store/zq0h41l75vlb4z45kzgjjmsjxvcv1qk7-mesa-6.4' since it is still alive
```

# Operation `--query`

## Synopsis

`nix-store` {`--query` | `-q`}
  {`--outputs` | `--requisites` | `-R` | `--references` |
  `--referrers` | `--referrers-closure` | `--deriver` | `-d` |
  `--graph` | `--tree` | `--binding` *name* | `-b` *name* | `--hash` |
  `--size` | `--roots`}
  [`--use-output`] [`-u`] [`--force-realise`] [`-f`]
  *paths…*

## Description

The operation `--query` displays various bits of information about the
store paths . The queries are described below. At most one query can be
specified. The default query is `--outputs`.

The paths *paths* may also be symlinks from outside of the Nix store, to
the Nix store. In that case, the query is applied to the target of the
symlink.

## Common query options

  - `--use-output`; `-u`\
    For each argument to the query that is a store derivation, apply the
    query to the output path of the derivation instead.

  - `--force-realise`; `-f`\
    Realise each argument to the query first (see [`nix-store
    --realise`](#operation---realise)).

## Queries

  - `--outputs`\
    Prints out the [output paths](../glossary.md) of the store
    derivations *paths*. These are the paths that will be produced when
    the derivation is built.

  - `--requisites`; `-R`\
    Prints out the [closure](../glossary.md) of the store path *paths*.

    This query has one option:

      - `--include-outputs`
        Also include the output path of store derivations, and their
        closures.

    This query can be used to implement various kinds of deployment. A
    *source deployment* is obtained by distributing the closure of a
    store derivation. A *binary deployment* is obtained by distributing
    the closure of an output path. A *cache deployment* (combined
    source/binary deployment, including binaries of build-time-only
    dependencies) is obtained by distributing the closure of a store
    derivation and specifying the option `--include-outputs`.

  - `--references`\
    Prints the set of [references](../glossary.md) of the store paths
    *paths*, that is, their immediate dependencies. (For *all*
    dependencies, use `--requisites`.)

  - `--referrers`\
    Prints the set of *referrers* of the store paths *paths*, that is,
    the store paths currently existing in the Nix store that refer to
    one of *paths*. Note that contrary to the references, the set of
    referrers is not constant; it can change as store paths are added or
    removed.

  - `--referrers-closure`\
    Prints the closure of the set of store paths *paths* under the
    referrers relation; that is, all store paths that directly or
    indirectly refer to one of *paths*. These are all the path currently
    in the Nix store that are dependent on *paths*.

  - `--deriver`; `-d`\
    Prints the [deriver](../glossary.md) of the store paths *paths*. If
    the path has no deriver (e.g., if it is a source file), or if the
    deriver is not known (e.g., in the case of a binary-only
    deployment), the string `unknown-deriver` is printed.

  - `--graph`\
    Prints the references graph of the store paths *paths* in the format
    of the `dot` tool of AT\&T's [Graphviz
    package](http://www.graphviz.org/). This can be used to visualise
    dependency graphs. To obtain a build-time dependency graph, apply
    this to a store derivation. To obtain a runtime dependency graph,
    apply it to an output path.

  - `--tree`\
    Prints the references graph of the store paths *paths* as a nested
    ASCII tree. References are ordered by descending closure size; this
    tends to flatten the tree, making it more readable. The query only
    recurses into a store path when it is first encountered; this
    prevents a blowup of the tree representation of the graph.

  - `--graphml`\
    Prints the references graph of the store paths *paths* in the
    [GraphML](http://graphml.graphdrawing.org/) file format. This can be
    used to visualise dependency graphs. To obtain a build-time
    dependency graph, apply this to a store derivation. To obtain a
    runtime dependency graph, apply it to an output path.

  - `--binding` *name*; `-b` *name*\
    Prints the value of the attribute *name* (i.e., environment
    variable) of the store derivations *paths*. It is an error for a
    derivation to not have the specified attribute.

  - `--hash`\
    Prints the SHA-256 hash of the contents of the store paths *paths*
    (that is, the hash of the output of `nix-store --dump` on the given
    paths). Since the hash is stored in the Nix database, this is a fast
    operation.

  - `--size`\
    Prints the size in bytes of the contents of the store paths *paths*
    — to be precise, the size of the output of `nix-store --dump` on
    the given paths. Note that the actual disk space required by the
    store paths may be higher, especially on filesystems with large
    cluster sizes.

  - `--roots`\
    Prints the garbage collector roots that point, directly or
    indirectly, at the store paths *paths*.

## Examples

Print the closure (runtime dependencies) of the `svn` program in the
current user environment:

```console
$ nix-store -qR $(which svn)
/nix/store/5mbglq5ldqld8sj57273aljwkfvj22mc-subversion-1.1.4
/nix/store/9lz9yc6zgmc0vlqmn2ipcpkjlmbi51vv-glibc-2.3.4
...
```

Print the build-time dependencies of `svn`:

```console
$ nix-store -qR $(nix-store -qd $(which svn))
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
$ nix-store -q --tree $(nix-store -qd $(which svn))
/nix/store/7i5082kfb6yjbqdbiwdhhza0am2xvh6c-subversion-1.1.4.drv
+---/nix/store/d8afh10z72n8l1cr5w42366abiblgn54-builder.sh
+---/nix/store/fmzxmpjx2lh849ph0l36snfj9zdibw67-bash-3.0.drv
|   +---/nix/store/570hmhmx3v57605cqg9yfvvyh0nnb8k8-bash
|   +---/nix/store/p3srsbd8dx44v2pg6nbnszab5mcwx03v-builder.sh
...
```

Show all paths that depend on the same OpenSSL library as `svn`:

```console
$ nix-store -q --referrers $(nix-store -q --binding openssl $(nix-store -qd $(which svn)))
/nix/store/23ny9l9wixx21632y2wi4p585qhva1q8-sylpheed-1.0.0
/nix/store/5mbglq5ldqld8sj57273aljwkfvj22mc-subversion-1.1.4
/nix/store/dpmvp969yhdqs7lm2r1a3gng7pyq6vy4-subversion-1.1.3
/nix/store/l51240xqsgg8a7yrbqdx1rfzyv6l26fx-lynx-2.8.5
```

Show all paths that directly or indirectly depend on the Glibc (C
library) used by `svn`:

```console
$ nix-store -q --referrers-closure $(ldd $(which svn) | grep /libc.so | awk '{print $3}')
/nix/store/034a6h4vpz9kds5r6kzb9lhh81mscw43-libgnomeprintui-2.8.2
/nix/store/15l3yi0d45prm7a82pcrknxdh6nzmxza-gawk-3.1.4
...
```

Note that `ldd` is a command that prints out the dynamic libraries used
by an ELF executable.

Make a picture of the runtime dependency graph of the current user
environment:

```console
$ nix-store -q --graph ~/.nix-profile | dot -Tps > graph.ps
$ gv graph.ps
```

Show every garbage collector root that points to a store path that
depends on `svn`:

```console
$ nix-store -q --roots $(which svn)
/nix/var/nix/profiles/default-81-link
/nix/var/nix/profiles/default-82-link
/nix/var/nix/profiles/per-user/eelco/profile-97-link
```

# Operation `--add`

## Synopsis

`nix-store` `--add` *paths…*

## Description

The operation `--add` adds the specified paths to the Nix store. It
prints the resulting paths in the Nix store on standard output.

## Example

```console
$ nix-store --add ./foo.c
/nix/store/m7lrha58ph6rcnv109yzx1nk1cj7k7zf-foo.c
```

# Operation `--add-fixed`

## Synopsis

`nix-store` `--add-fixed` [`--recursive`] *algorithm* *paths…*

## Description

The operation `--add-fixed` adds the specified paths to the Nix store.
Unlike `--add` paths are registered using the specified hashing
algorithm, resulting in the same output path as a fixed-output
derivation. This can be used for sources that are not available from a
public url or broke since the download expression was written.

This operation has the following options:

  - `--recursive`\
    Use recursive instead of flat hashing mode, used when adding
    directories to the store.

## Example

```console
$ nix-store --add-fixed sha256 ./hello-2.10.tar.gz
/nix/store/3x7dwzq014bblazs7kq20p9hyzz0qh8g-hello-2.10.tar.gz
```

# Operation `--verify`

## Synopsis

`nix-store` `--verify` [`--check-contents`] [`--repair`]

## Description

The operation `--verify` verifies the internal consistency of the Nix
database, and the consistency between the Nix database and the Nix
store. Any inconsistencies encountered are automatically repaired.
Inconsistencies are generally the result of the Nix store or database
being modified by non-Nix tools, or of bugs in Nix itself.

This operation has the following options:

  - `--check-contents`\
    Checks that the contents of every valid store path has not been
    altered by computing a SHA-256 hash of the contents and comparing it
    with the hash stored in the Nix database at build time. Paths that
    have been modified are printed out. For large stores,
    `--check-contents` is obviously quite slow.

  - `--repair`\
    If any valid path is missing from the store, or (if
    `--check-contents` is given) the contents of a valid path has been
    modified, then try to repair the path by redownloading it. See
    `nix-store --repair-path` for details.

# Operation `--verify-path`

## Synopsis

`nix-store` `--verify-path` *paths…*

## Description

The operation `--verify-path` compares the contents of the given store
paths to their cryptographic hashes stored in Nix’s database. For every
changed path, it prints a warning message. The exit status is 0 if no
path has changed, and 1 otherwise.

## Example

To verify the integrity of the `svn` command and all its dependencies:

```console
$ nix-store --verify-path $(nix-store -qR $(which svn))
```

# Operation `--repair-path`

## Synopsis

`nix-store` `--repair-path` *paths…*

## Description

The operation `--repair-path` attempts to “repair” the specified paths
by redownloading them using the available substituters. If no
substitutes are available, then repair is not possible.

> **Warning**
>
> During repair, there is a very small time window during which the old
> path (if it exists) is moved out of the way and replaced with the new
> path. If repair is interrupted in between, then the system may be left
> in a broken state (e.g., if the path contains a critical system
> component like the GNU C Library).

## Example

```console
$ nix-store --verify-path /nix/store/dj7a81wsm1ijwwpkks3725661h3263p5-glibc-2.13
path `/nix/store/dj7a81wsm1ijwwpkks3725661h3263p5-glibc-2.13' was modified!
  expected hash `2db57715ae90b7e31ff1f2ecb8c12ec1cc43da920efcbe3b22763f36a1861588',
  got `481c5aa5483ebc97c20457bb8bca24deea56550d3985cda0027f67fe54b808e4'

$ nix-store --repair-path /nix/store/dj7a81wsm1ijwwpkks3725661h3263p5-glibc-2.13
fetching path `/nix/store/d7a81wsm1ijwwpkks3725661h3263p5-glibc-2.13'...
…
```

# Operation `--dump`

## Synopsis

`nix-store` `--dump` *path*

## Description

The operation `--dump` produces a NAR (Nix ARchive) file containing the
contents of the file system tree rooted at *path*. The archive is
written to standard output.

A NAR archive is like a TAR or Zip archive, but it contains only the
information that Nix considers important. For instance, timestamps are
elided because all files in the Nix store have their timestamp set to 0
anyway. Likewise, all permissions are left out except for the execute
bit, because all files in the Nix store have 444 or 555 permission.

Also, a NAR archive is *canonical*, meaning that “equal” paths always
produce the same NAR archive. For instance, directory entries are
always sorted so that the actual on-disk order doesn’t influence the
result.  This means that the cryptographic hash of a NAR dump of a
path is usable as a fingerprint of the contents of the path. Indeed,
the hashes of store paths stored in Nix’s database (see `nix-store -q
--hash`) are SHA-256 hashes of the NAR dump of each store path.

NAR archives support filenames of unlimited length and 64-bit file
sizes. They can contain regular files, directories, and symbolic links,
but not other types of files (such as device nodes).

A Nix archive can be unpacked using `nix-store
--restore`.

# Operation `--restore`

## Synopsis

`nix-store` `--restore` *path*

## Description

The operation `--restore` unpacks a NAR archive to *path*, which must
not already exist. The archive is read from standard input.

# Operation `--export`

## Synopsis

`nix-store` `--export` *paths…*

## Description

The operation `--export` writes a serialisation of the specified store
paths to standard output in a format that can be imported into another
Nix store with `nix-store --import`. This is like `nix-store
--dump`, except that the NAR archive produced by that command doesn’t
contain the necessary meta-information to allow it to be imported into
another Nix store (namely, the set of references of the path).

This command does not produce a *closure* of the specified paths, so if
a store path references other store paths that are missing in the target
Nix store, the import will fail. To copy a whole closure, do something
like:

```console
$ nix-store --export $(nix-store -qR paths) > out
```

To import the whole closure again, run:

```console
$ nix-store --import < out
```

# Operation `--import`

## Synopsis

`nix-store` `--import`

## Description

The operation `--import` reads a serialisation of a set of store paths
produced by `nix-store --export` from standard input and adds those
store paths to the Nix store. Paths that already exist in the Nix store
are ignored. If a path refers to another path that doesn’t exist in the
Nix store, the import fails.

# Operation `--optimise`

## Synopsis

`nix-store` `--optimise`

## Description

The operation `--optimise` reduces Nix store disk space usage by finding
identical files in the store and hard-linking them to each other. It
typically reduces the size of the store by something like 25-35%. Only
regular files and symlinks are hard-linked in this manner. Files are
considered identical when they have the same NAR archive serialisation:
that is, regular files must have the same contents and permission
(executable or non-executable), and symlinks must have the same
contents.

After completion, or when the command is interrupted, a report on the
achieved savings is printed on standard error.

Use `-vv` or `-vvv` to get some progress indication.

## Example

```console
$ nix-store --optimise
hashing files in `/nix/store/qhqx7l2f1kmwihc9bnxs7rc159hsxnf3-gcc-4.1.1'
...
541838819 bytes (516.74 MiB) freed by hard-linking 54143 files;
there are 114486 files with equal contents out of 215894 files in total
```

# Operation `--read-log`

## Synopsis

`nix-store` {`--read-log` | `-l`} *paths…*

## Description

The operation `--read-log` prints the build log of the specified store
paths on standard output. The build log is whatever the builder of a
derivation wrote to standard output and standard error. If a store path
is not a derivation, the deriver of the store path is used.

Build logs are kept in `/nix/var/log/nix/drvs`. However, there is no
guarantee that a build log is available for any particular store path.
For instance, if the path was downloaded as a pre-built binary through a
substitute, then the log is unavailable.

## Example

```console
$ nix-store -l $(which ktorrent)
building /nix/store/dhc73pvzpnzxhdgpimsd9sw39di66ph1-ktorrent-2.2.1
unpacking sources
unpacking source archive /nix/store/p8n1jpqs27mgkjw07pb5269717nzf5f8-ktorrent-2.2.1.tar.gz
ktorrent-2.2.1/
ktorrent-2.2.1/NEWS
...
```

# Operation `--dump-db`

## Synopsis

`nix-store` `--dump-db` [*paths…*]

## Description

The operation `--dump-db` writes a dump of the Nix database to standard
output. It can be loaded into an empty Nix store using `--load-db`. This
is useful for making backups and when migrating to different database
schemas.

By default, `--dump-db` will dump the entire Nix database. When one or
more store paths is passed, only the subset of the Nix database for
those store paths is dumped. As with `--export`, the user is responsible
for passing all the store paths for a closure. See `--export` for an
example.

# Operation `--load-db`

## Synopsis

`nix-store` `--load-db`

## Description

The operation `--load-db` reads a dump of the Nix database created by
`--dump-db` from standard input and loads it into the Nix database.

# Operation `--print-env`

## Synopsis

`nix-store` `--print-env` *drvpath*

## Description

The operation `--print-env` prints out the environment of a derivation
in a format that can be evaluated by a shell. The command line arguments
of the builder are placed in the variable `_args`.

## Example

```console
$ nix-store --print-env $(nix-instantiate '<nixpkgs>' -A firefox)
…
export src; src='/nix/store/plpj7qrwcz94z2psh6fchsi7s8yihc7k-firefox-12.0.source.tar.bz2'
export stdenv; stdenv='/nix/store/7c8asx3yfrg5dg1gzhzyq2236zfgibnn-stdenv'
export system; system='x86_64-linux'
export _args; _args='-e /nix/store/9krlzvny65gdc8s7kpb6lkx8cd02c25c-default-builder.sh'
```

# Operation `--generate-binary-cache-key`

## Synopsis

`nix-store` `--generate-binary-cache-key` *key-name* *secret-key-file* *public-key-file*

## Description

This command generates an [Ed25519 key pair](http://ed25519.cr.yp.to/)
that can be used to create a signed binary cache. It takes three
mandatory parameters:

1.  A key name, such as `cache.example.org-1`, that is used to look up
    keys on the client when it verifies signatures. It can be anything,
    but it’s suggested to use the host name of your cache (e.g.
    `cache.example.org`) with a suffix denoting the number of the key
    (to be incremented every time you need to revoke a key).

2.  The file name where the secret key is to be stored.

3.  The file name where the public key is to be stored.
