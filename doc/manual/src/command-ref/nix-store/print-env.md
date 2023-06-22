# Name

`nix-store --print-env` - print the build environment of a derivation

## Synopsis

`nix-store` `--print-env` *drvpath*

## Description

The operation `--print-env` prints out the environment of a derivation
in a format that can be evaluated by a shell. The command line arguments
of the builder are placed in the variable `_args`.

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ../env-common.md}}

## Example

```console
$ nix-store --print-env $(nix-instantiate '<nixpkgs>' -A firefox)
…
export src; src='/nix/store/plpj7qrwcz94z2psh6fchsi7s8yihc7k-firefox-12.0.source.tar.bz2'
export stdenv; stdenv='/nix/store/7c8asx3yfrg5dg1gzhzyq2236zfgibnn-stdenv'
export system; system='x86_64-linux'
export _args; _args='-e /nix/store/9krlzvny65gdc8s7kpb6lkx8cd02c25c-default-builder.sh'
```

