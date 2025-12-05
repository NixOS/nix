# Name

`nix-instantiate` - instantiate store derivations from Nix expressions

# Synopsis

`nix-instantiate`
  [`--parse` | `--eval` [`--strict`] [`--raw` | `--json` | `--xml`] ]
  [`--read-write-mode`]
  [`--arg` *name* *value*]
  [{`--attr`| `-A`} *attrPath*]
  [`--add-root` *path*]
  [`--expr` | `-E`]
  *files…*

`nix-instantiate` `--find-file` *files…*

# Description

The command `nix-instantiate` produces [store derivation]s from (high-level) Nix expressions.
It evaluates the Nix expressions in each of *files* (which defaults to
*./default.nix*). Each top-level expression should evaluate to a
derivation, a list of derivations, or a set of derivations. The paths
of the resulting store derivations are printed on standard output.

[store derivation]: @docroot@/glossary.md#gloss-store-derivation

If *files* is the character `-`, then a Nix expression will be read from
standard input.

# Options

- `--add-root` *path*

  See the [corresponding option](./nix-store.md) in `nix-store`.

- `--parse`

  Just parse the input files, and print their abstract syntax trees on
  standard output as a Nix expression.

- `--eval`

  Just parse and evaluate the input files, and print the resulting
  values on standard output.
  Store derivations are not serialized and written to the store, but instead just hashed and discarded.

  > **Warning**
  >
  > This option produces output which can be parsed as a Nix expression which
  > will produce a different result than the input expression when evaluated.
  > For example, these two Nix expressions print the same result despite
  > having different meaning:
  >
  > ```console
  > $ nix-instantiate --eval --expr '{ a = {}; }'
  > { a = <CODE>; }
  > $ nix-instantiate --eval --expr '{ a = <CODE>; }'
  > { a = <CODE>; }
  > ```
  >
  > For human-readable output, `nix eval` (experimental) is more informative:
  >
  > ```console
  > $ nix-instantiate --eval --expr 'a: a'
  > <LAMBDA>
  > $ nix eval --expr 'a: a'
  > «lambda @ «string»:1:1»
  > ```
  >
  > For machine-readable output, the `--xml` option produces unambiguous
  > output:
  >
  > ```console
  > $ nix-instantiate --eval --xml --expr '{ foo = <CODE>; }'
  > <?xml version='1.0' encoding='utf-8'?>
  > <expr>
  >   <attrs>
  >     <attr column="3" line="1" name="foo">
  >       <unevaluated />
  >     </attr>
  >   </attrs>
  > </expr>
  > ```

- `--find-file`

  Look up the given files in Nix’s search path (as specified by the
  `NIX_PATH` environment variable). If found, print the corresponding
  absolute paths on standard output. For instance, if `NIX_PATH` is
  `nixpkgs=/home/alice/nixpkgs`, then `nix-instantiate --find-file
  nixpkgs/default.nix` will print `/home/alice/nixpkgs/default.nix`.

- `--strict`

  When used with `--eval`, recursively evaluate list elements and
  attributes. Normally, such sub-expressions are left unevaluated
  (since the Nix language is lazy).

  > **Warning**
  >
  > This option can cause non-termination, because lazy data
  > structures can be infinitely large.

- `--raw`

  When used with `--eval`, the evaluation result must be a string,
  which is printed verbatim, without quoting, escaping or trailing newline.

- `--json`

  When used with `--eval`, print the resulting value as an JSON
  representation of the abstract syntax tree rather than as a Nix expression.

- `--xml`

  When used with `--eval`, print the resulting value as an XML
  representation of the abstract syntax tree rather than as a Nix expression.
  The schema is the same as that used by the [`toXML`
  built-in](../language/builtins.md).

- `--read-write-mode`

  When used with `--eval`, perform evaluation in read/write mode so
  nix language features that require it will still work (at the cost
  of needing to do instantiation of every evaluated derivation). If
  this option is not enabled, there may be uninstantiated store paths
  in the final output.

{{#include ./opt-common.md}}

{{#include ./env-common.md}}

# Examples

Instantiate [store derivation]s from a Nix expression, and build them using `nix-store`:

```console
$ nix-instantiate test.nix (instantiate)
/nix/store/cigxbmvy6dzix98dxxh9b6shg7ar5bvs-perl-BerkeleyDB-0.26.drv

$ nix-store --realise $(nix-instantiate test.nix) (build)
...
/nix/store/qhqk4n8ci095g3sdp93x7rgwyh9rdvgk-perl-BerkeleyDB-0.26 (output path)

$ ls -l /nix/store/qhqk4n8ci095g3sdp93x7rgwyh9rdvgk-perl-BerkeleyDB-0.26
dr-xr-xr-x    2 eelco    users        4096 1970-01-01 01:00 lib
...
```

You can also give a Nix expression on the command line:

```console
$ nix-instantiate --expr 'with import <nixpkgs> { }; hello'
/nix/store/j8s4zyv75a724q38cb0r87rlczaiag4y-hello-2.8.drv
```

This is equivalent to:

```console
$ nix-instantiate '<nixpkgs>' --attr hello
```

Parsing and evaluating Nix expressions:

```console
$ nix-instantiate --parse --expr '1 + 2'
1 + 2
```

```console
$ nix-instantiate --eval --expr '1 + 2'
3
```

```console
$ nix-instantiate --eval --xml --expr '1 + 2'
<?xml version='1.0' encoding='utf-8'?>
<expr>
  <int value="3" />
</expr>
```

The difference between non-strict and strict evaluation:

```console
$ nix-instantiate --eval --xml --expr '{ x = {}; }'
<?xml version='1.0' encoding='utf-8'?>
<expr>
  <attrs>
    <attr column="3" line="1" name="x">
      <unevaluated />
    </attr>
  </attrs>
</expr>

$ nix-instantiate --eval --xml --strict --expr '{ x = {}; }'
<?xml version='1.0' encoding='utf-8'?>
<expr>
  <attrs>
    <attr column="3" line="1" name="x">
      <attrs>
      </attrs>
    </attr>
  </attrs>
</expr>
```
