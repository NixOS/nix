Title: nix-instantiate

# Name

`nix-instantiate` - instantiate store derivations from Nix expressions

# Synopsis

`nix-instantiate`
  [`--parse` | `--eval` [`--strict`] [`--json`] [`--xml`] ]
  [`--read-write-mode`]
  [`--arg` *name* *value*]
  [{`--attr`| `-A`} *attrPath*]
  [`--add-root` *path*]
  [`--expr` | `-E`]
  *files…*

`nix-instantiate` `--find-file` *files…*

# Description

The command `nix-instantiate` generates [store
derivations](../glossary.md) from (high-level) Nix expressions. It
evaluates the Nix expressions in each of *files* (which defaults to
*./default.nix*). Each top-level expression should evaluate to a
derivation, a list of derivations, or a set of derivations. The paths
of the resulting store derivations are printed on standard output.

If *files* is the character `-`, then a Nix expression will be read from
standard input.

# Options

  - `--add-root` *path*  
    See the [corresponding option](nix-store.md) in `nix-store`.

  - `--parse`  
    Just parse the input files, and print their abstract syntax trees on
    standard output in ATerm format.

  - `--eval`  
    Just parse and evaluate the input files, and print the resulting
    values on standard output. No instantiation of store derivations
    takes place.

  - `--find-file`  
    Look up the given files in Nix’s search path (as specified by the
    `NIX_PATH` environment variable). If found, print the corresponding
    absolute paths on standard output. For instance, if `NIX_PATH` is
    `nixpkgs=/home/alice/nixpkgs`, then `nix-instantiate --find-file
    nixpkgs/default.nix` will print `/home/alice/nixpkgs/default.nix`.

  - `--strict`  
    When used with `--eval`, recursively evaluate list elements and
    attributes. Normally, such sub-expressions are left unevaluated
    (since the Nix expression language is lazy).

    > **Warning**
    >
    > This option can cause non-termination, because lazy data
    > structures can be infinitely large.

  - `--json`  
    When used with `--eval`, print the resulting value as an JSON
    representation of the abstract syntax tree rather than as an ATerm.

  - `--xml`  
    When used with `--eval`, print the resulting value as an XML
    representation of the abstract syntax tree rather than as an ATerm.
    The schema is the same as that used by the [`toXML`
    built-in](../expressions/builtins.md).

  - `--read-write-mode`  
    When used with `--eval`, perform evaluation in read/write mode so
    nix language features that require it will still work (at the cost
    of needing to do instantiation of every evaluated derivation). If
    this option is not enabled, there may be uninstantiated store paths
    in the final output.

<!-- end list -->

# Examples

Instantiating store derivations from a Nix expression, and building them
using `nix-store`:

```console
$ nix-instantiate test.nix (instantiate)
/nix/store/cigxbmvy6dzix98dxxh9b6shg7ar5bvs-perl-BerkeleyDB-0.26.drv

$ nix-store -r $(nix-instantiate test.nix) (build)
...
/nix/store/qhqk4n8ci095g3sdp93x7rgwyh9rdvgk-perl-BerkeleyDB-0.26 (output path)

$ ls -l /nix/store/qhqk4n8ci095g3sdp93x7rgwyh9rdvgk-perl-BerkeleyDB-0.26
dr-xr-xr-x    2 eelco    users        4096 1970-01-01 01:00 lib
...
```

You can also give a Nix expression on the command line:

```console
$ nix-instantiate -E 'with import <nixpkgs> { }; hello'
/nix/store/j8s4zyv75a724q38cb0r87rlczaiag4y-hello-2.8.drv
```

This is equivalent to:

```console
$ nix-instantiate '<nixpkgs>' -A hello
```

Parsing and evaluating Nix expressions:

```console
$ nix-instantiate --parse -E '1 + 2'
1 + 2
```

```console
$ nix-instantiate --eval -E '1 + 2'
3
```

```console
$ nix-instantiate --eval --xml -E '1 + 2'
<?xml version='1.0' encoding='utf-8'?>
<expr>
  <int value="3" />
</expr>
```

The difference between non-strict and strict evaluation:

```console
$ nix-instantiate --eval --xml -E 'rec { x = "foo"; y = x; }'
...
  <attr name="x">
    <string value="foo" />
  </attr>
  <attr name="y">
    <unevaluated />
  </attr>
...
```

Note that `y` is left unevaluated (the XML representation doesn’t
attempt to show non-normal forms).

```console
$ nix-instantiate --eval --xml --strict -E 'rec { x = "foo"; y = x; }'
...
  <attr name="x">
    <string value="foo" />
  </attr>
  <attr name="y">
    <string value="foo" />
  </attr>
...
```
