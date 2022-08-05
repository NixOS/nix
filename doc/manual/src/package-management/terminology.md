# Terminology

A *local store* exists on the local filesystem of the machine where
Nix is invoked.  The `/nix/store` directory is one example of a
local store.  You can use other local stores by passing the
`--store` flag to `nix`.

A *remote store* is a store which exists anywhere other than the
local filesystem.  One example is the `/nix/store` directory on
another machine, accessed via `ssh` or served by the `nix-serve`
Perl script.

A *binary cache* is a remote store which is not the local store of
any machine.  Examples of binary caches include S3 buckets and the
[NixOS binary cache](https://cache.nixos.org).  Binary caches use a
disk layout that is different from local stores; in particular, they
keep metadata and signatures in `.narinfo` files rather than in
`/nix/var/nix/db`.

A *substituter* is a store other than `/nix/store` from which Nix will
copy a store path instead of building it.  Nix will not copy a store
path from a remote store unless one of the following is true:

- the store object is signed by one of the `trusted-public-keys`
- the substituter is in the `trusted-substituters` list
- the `no-require-sigs` option has been set to disable signature checking
- the store object is a derivation
- the store object is the realisation of a fixed-output derivation
