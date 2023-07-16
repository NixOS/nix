# Python Bindings

This directory contains experimental Python bindings to a small subset of Nix's functionality. These bindings are very fast since they link to the necessary dynamic libraries directly, without having to call the Nix CLI for every operation.

Thanks to [@Mic92](https://github.com/Mic92) who wrote [Pythonix](https://github.com/Mic92/pythonix) which these bindings were originally based on, before they became the official bindings that are part of the Nix project. They were upstreamed to decrease maintenance overhead and make sure they are always up-to-date.

Note that the Python bindings are new and experimental. The interface is likely to change based on known issues and user feedback.

## Documentation

See [index.md](./doc/index.md), which is also rendered in the HTML manual.

To hack on these bindings, see [hacking.md](./doc/hacking.md), also rendered in the HTML manual.
