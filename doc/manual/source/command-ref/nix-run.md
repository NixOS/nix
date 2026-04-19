# Name

`nix-run` - run an executable from a Nix expression

# Synopsis

`nix-run`
  [{`--attr` | `-A`} *attrPath*]
  [`--expr` | `-E`]
  [`--arg` *name* *value*]
  [*file*]
  [`--` *args*...]

# Description

The command `nix-run` evaluates a Nix expression, selects a derivation
(optionally via `--attr`), and executes its main program. The
executable is chosen the same way `nix run` does when given a
derivation: `meta.mainProgram` is preferred, falling back to `pname`,
and finally to the parsed derivation name. This matches the semantics
of `lib.getExe` in nixpkgs.

*file* defaults to `./default.nix`. When `--expr` is given, the
positional argument is interpreted as a Nix expression instead of a
file path. A literal `--` ends option parsing; everything after it is
passed verbatim to the program.

# Examples

Run `hello` from `<nixpkgs>`:

```console
$ nix-run '<nixpkgs>' -A hello
Hello, world!
```

Evaluate an expression and pass arguments to the program:

```console
$ nix-run -E 'import <nixpkgs> {}' -A hello -- --greeting=hi
hi
```

Run the package produced by a local `release.nix`:

```console
$ nix-run ./release.nix -A myApp
```
