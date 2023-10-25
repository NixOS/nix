# Nix Store

The *Nix store* is an abstraction used by Nix to store immutable filesystem artifacts (such as software packages) that can have dependencies (*references*) between them.
There are multiple implementations of the Nix store, such as the actual filesystem (`/nix/store`) and binary caches.
