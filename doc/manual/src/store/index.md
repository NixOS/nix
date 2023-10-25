# Nix Store

The *Nix store* is an abstraction to store immutable file system data (such as software packages) that can have dependencies on other such data.

There are multiple implementations of Nix stores with different capabilities, such as the actual filesystem (`/nix/store`) or binary caches.
