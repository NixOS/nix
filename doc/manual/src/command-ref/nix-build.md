# Name

`nix-build` - build a Nix expression

# Synopsis

`nix-build` [*paths…*]
  [`--arg` *name* *value*]
  [`--argstr` *name* *value*]
  [{`--attr` | `-A`} *attrPath*]
  [`--no-out-link`]
  [`--dry-run`]
  [{`--out-link` | `-o`} *outlink*]

# Description

The `nix-build` command builds the derivations described by the Nix
expressions in *paths*. If the build succeeds, it places a symlink to
the result in the current directory. The symlink is called `result`. If
there are multiple Nix expressions, or the Nix expressions evaluate to
multiple derivations, multiple sequentially numbered symlinks are
created (`result`, `result-2`, and so on).

If no *paths* are specified, then `nix-build` will use `default.nix` in
the current directory, if it exists.

If an element of *paths* starts with `http://` or `https://`, it is
interpreted as the URL of a tarball that will be downloaded and unpacked
to a temporary location. The tarball must include a single top-level
directory containing at least a file named `default.nix`.

`nix-build` is essentially a wrapper around
[`nix-instantiate`](nix-instantiate.md) (to translate a high-level Nix
expression to a low-level store derivation) and [`nix-store
--realise`](nix-store.md#operation---realise) (to build the store
derivation).

> **Warning**
>
> The result of the build is automatically registered as a root of the
> Nix garbage collector. This root disappears automatically when the
> `result` symlink is deleted or renamed. So don’t rename the symlink.

# Options

All options not listed here are passed to `nix-store
--realise`, except for `--arg` and `--attr` / `-A` which are passed to
`nix-instantiate`.

  - `--no-out-link`\
    Do not create a symlink to the output path. Note that as a result
    the output does not become a root of the garbage collector, and so
    might be deleted by `nix-store
                    --gc`.

  - `--dry-run`\
    Show what store paths would be built or downloaded.

  - `--out-link` / `-o` *outlink*\
    Change the name of the symlink to the output path created from
    `result` to *outlink*.

The following common options are supported:

# Examples

```console
$ nix-build '<nixpkgs>' -A firefox
store derivation is /nix/store/qybprl8sz2lc...-firefox-1.5.0.7.drv
/nix/store/d18hyl92g30l...-firefox-1.5.0.7

$ ls -l result
lrwxrwxrwx  ...  result -> /nix/store/d18hyl92g30l...-firefox-1.5.0.7

$ ls ./result/bin/
firefox  firefox-config
```

If a derivation has multiple outputs, `nix-build` will build the default
(first) output. You can also build all outputs:

```console
$ nix-build '<nixpkgs>' -A openssl.all
```

This will create a symlink for each output named `result-outputname`.
The suffix is omitted if the output name is `out`. So if `openssl` has
outputs `out`, `bin` and `man`, `nix-build` will create symlinks
`result`, `result-bin` and `result-man`. It’s also possible to build a
specific output:

```console
$ nix-build '<nixpkgs>' -A openssl.man
```

This will create a symlink `result-man`.

Build a Nix expression given on the command line:

```console
$ nix-build -E 'with import <nixpkgs> { }; runCommand "foo" { } "echo bar > $out"'
$ cat ./result
bar
```

Build the GNU Hello package from the latest revision of the master
branch of Nixpkgs:

```console
$ nix-build https://github.com/NixOS/nixpkgs/archive/master.tar.gz -A hello
```
