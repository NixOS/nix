# Nix Store

The *Nix store* is an abstraction to store immutable file system data (such as software packages) that can have dependencies on other such data.

There are [multiple types of Nix stores](./types/index.md) with different capabilities, such as the default one on the [local filesystem](./types/local-store.md) (`/nix/store`) or [binary caches](./types/http-binary-cache-store.md).
