# Name

`nix-store --read-log` - print build log

# Synopsis

`nix-store` {`--read-log` | `-l`} *paths…*

# Description

The operation `--read-log` prints the build log of the specified store
paths on standard output. The build log is whatever the builder of a
derivation wrote to standard output and standard error. If a store path
is not a derivation, the deriver of the store path is used.

Build logs are kept in `/nix/var/log/nix/drvs`. However, there is no
guarantee that a build log is available for any particular store path.
For instance, if the path was downloaded as a pre-built binary through a
substitute, then the log is unavailable.

{{#include ./opt-common.md}}

{{#include ../opt-common.md}}

{{#include ../env-common.md}}

# Example

```console
$ nix-store --read-log $(which ktorrent)
building /nix/store/dhc73pvzpnzxhdgpimsd9sw39di66ph1-ktorrent-2.2.1
unpacking sources
unpacking source archive /nix/store/p8n1jpqs27mgkjw07pb5269717nzf5f8-ktorrent-2.2.1.tar.gz
ktorrent-2.2.1/
ktorrent-2.2.1/NEWS
...
```

