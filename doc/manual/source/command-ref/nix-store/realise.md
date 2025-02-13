# Name

`nix-store --realise` - build or fetch store objects

# Synopsis

`nix-store` {`--realise` | `-r`} *paths…* [`--dry-run`]

# Description


Each of *paths* is processed as follows:

- If the path leads to a [store derivation]:
  1. If it is not [valid], substitute the store derivation file itself.
  2. Realise its [output paths]:
    - Try to fetch from [substituters] the [store objects] associated with the output paths in the store derivation's [closure].
      - With [content-addressing derivations] (experimental):
        Determine the output paths to realise by querying content-addressed realisation entries in the [Nix database].
    - For any store paths that cannot be substituted, produce the required store objects:
      1. Realise all outputs of the derivation's dependencies
      2. Run the derivation's [`builder`](@docroot@/language/derivations.md#attr-builder) executable
         <!-- TODO: Link to build process page #8888 -->
- Otherwise, and if the path is not already valid: Try to fetch the associated [store objects] in the path's [closure] from [substituters].

If no substitutes are available and no store derivation is given, realisation fails.

[store paths]: @docroot@/store/store-path.md
[valid]: @docroot@/glossary.md#gloss-validity
[store derivation]: @docroot@/glossary.md#gloss-store-derivation
[output paths]: @docroot@/glossary.md#gloss-output-path
[store objects]: @docroot@/store/store-object.md
[closure]: @docroot@/glossary.md#gloss-closure
[substituters]: @docroot@/command-ref/conf-file.md#conf-substituters
[content-addressing derivations]: @docroot@/development/experimental-features.md#xp-feature-ca-derivations
[Nix database]: @docroot@/glossary.md#gloss-nix-database

The resulting paths are printed on standard output.
For non-derivation arguments, the argument itself is printed.

{{#include ../status-build-failure.md}}

# Options

- `--dry-run`

  Print on standard error a description of what packages would be
  built or downloaded, without actually performing the operation.

- `--ignore-unknown`

  If a non-derivation path does not have a substitute, then silently
  ignore it.

- `--check`

  This option allows you to check whether a derivation is
  deterministic. It rebuilds the specified derivation and checks
  whether the result is bitwise-identical with the existing outputs,
  printing an error if that’s not the case. The outputs of the
  specified derivation must already exist. When used with `-K`, if an
  output path is not identical to the corresponding output from the
  previous build, the new output path is left in
  `/nix/store/name.check.`

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ../env-common.md}}

# Examples

This operation is typically used to build [store derivation]s produced by
[`nix-instantiate`](@docroot@/command-ref/nix-instantiate.md):

```console
$ nix-store --realise $(nix-instantiate ./test.nix)
/nix/store/31axcgrlbfsxzmfff1gyj1bf62hvkby2-aterm-2.3.1
```

This is essentially what [`nix-build`](@docroot@/command-ref/nix-build.md) does.

To test whether a previously-built derivation is deterministic:

```console
$ nix-build '<nixpkgs>' --attr hello --check -K
```

Use [`nix-store --read-log`](./read-log.md) to show the stderr and stdout of a build:

```console
$ nix-store --read-log $(nix-instantiate ./test.nix)
```
