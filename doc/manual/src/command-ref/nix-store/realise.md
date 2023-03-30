# Name

`nix-store --realise` - realise specified store paths

# Synopsis

`nix-store` {`--realise` | `-r`} *paths…* [`--dry-run`]

# Description

The operation `--realise` essentially “builds” the specified store
paths. Realisation is a somewhat overloaded term:

  - If the store path is a *derivation*, realisation ensures that the
    output paths of the derivation are [valid] (i.e.,
    the output path and its closure exist in the file system). This
    can be done in several ways. First, it is possible that the
    outputs are already valid, in which case we are done
    immediately. Otherwise, there may be [substitutes]
    that produce the outputs (e.g., by downloading them). Finally, the
    outputs can be produced by running the build task described
    by the derivation.

  - If the store path is not a derivation, realisation ensures that the
    specified path is valid (i.e., it and its closure exist in the file
    system). If the path is already valid, we are done immediately.
    Otherwise, the path and any missing paths in its closure may be
    produced through substitutes. If there are no (successful)
    substitutes, realisation fails.

[valid]: @docroot@/glossary.md#gloss-validity
[substitutes]: @docroot@/glossary.md#gloss-substitute

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
    derivation](@docroot@/language/advanced-attributes.md).

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


{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ../env-common.md}}

# Examples

This operation is typically used to build [store derivation]s produced by
[`nix-instantiate`](@docroot@/command-ref/nix-instantiate.md):

[store derivation]: @docroot@/glossary.md#gloss-store-derivation

```console
$ nix-store -r $(nix-instantiate ./test.nix)
/nix/store/31axcgrlbfsxzmfff1gyj1bf62hvkby2-aterm-2.3.1
```

This is essentially what [`nix-build`](@docroot@/command-ref/nix-build.md) does.

To test whether a previously-built derivation is deterministic:

```console
$ nix-build '<nixpkgs>' -A hello --check -K
```

Use [`nix-store --read-log`](./read-log.md) to show the stderr and stdout of a build:

```console
$ nix-store --read-log $(nix-instantiate ./test.nix)
```
