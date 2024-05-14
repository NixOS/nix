# Name

`nix-store --export` - export store paths to a Nix Archive

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
Nix store, the import will fail.

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ../env-common.md}}

# Examples

> **Example**
>
> Deploy GNU Hello to an airgapped machine via USB stick.
>
> Write the closure to the block device on a machine with internet connection:
>
> ```shell-session
> [alice@itchy]$ storePath=$(nix-build '<nixpkgs>' -I nixpkgs=channel:nixpkgs-unstable -A hello --no-out-link)
> [alice@itchy]$ nix-store --export $(nix-store --query --requisites $storePath) | sudo dd of=/dev/usb
> ```
>
> Read the closure from the block device on the machine without internet connection:
>
> ```shell-session
> [bob@scratchy]$ hello=$(sudo dd if=/dev/usb | nix-store --import | tail -1)
> [bob@scratchy]$ $hello/bin/hello
> Hello, world!
> ```
