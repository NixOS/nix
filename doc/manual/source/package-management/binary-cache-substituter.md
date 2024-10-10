# Serving a Nix store via HTTP

You can easily share the Nix store of a machine via HTTP. This allows
other machines to fetch store paths from that machine to speed up
installations. It uses the same *binary cache* mechanism that Nix
usually uses to fetch pre-built binaries from <https://cache.nixos.org>.

The daemon that handles binary cache requests via HTTP, `nix-serve`, is
not part of the Nix distribution, but you can install it from Nixpkgs:

```console
$ nix-env --install --attr nixpkgs.nix-serve
```

You can then start the server, listening for HTTP connections on
whatever port you like:

```console
$ nix-serve -p 8080
```

To check whether it works, try the following on the client:

```console
$ curl http://avalon:8080/nix-cache-info
```

which should print something like:

    StoreDir: /nix/store
    WantMassQuery: 1
    Priority: 30

On the client side, you can tell Nix to use your binary cache using
`--substituters`, e.g.:

```console
$ nix-env --install --attr nixpkgs.firefox --substituters http://avalon:8080/
```

The option `substituters` tells Nix to use this binary cache in
addition to your default caches, such as <https://cache.nixos.org>.
Thus, for any path in the closure of Firefox, Nix will first check if
the path is available on the server `avalon` or another binary caches.
If not, it will fall back to building from source.

You can also tell Nix to always use your binary cache by adding a line
to the `nix.conf` configuration file like this:

    substituters = http://avalon:8080/ https://cache.nixos.org/
