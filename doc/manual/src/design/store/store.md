A Nix store is a collection of *store objects* referred to by *store paths*.
Every store also has a "store directory path", which is a path prefix used for various purposes.

There are many types of stores, but all of them at least respect this model.
Some however offer additional functionality.
