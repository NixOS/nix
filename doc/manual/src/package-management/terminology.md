# Terminology

From   the  perspective   of   the  location   where  Nix   is
invoked<sup><b>1</b></sup>, the  Nix store can be  referred to
as a "_local_" or a "_remote_" one:

<sup>\[1]: Where "invoking Nix" means  an executing a Nix core
action/operation on  a Nix store.  For example, using  any CLI
commands from the `NixOS/nix` implementation.</sup>

+ A *local  store* exists  on the local  filesystem of
  the machine where Nix is  invoked. You can use other
  local stores  by passing  the `--store` flag  to the
  `nix` command.

+ A  *remote store*  exists  anywhere  other than  the
  local  filesystem. One  example is  the `/nix/store`
  directory on another machine,  accessed via `ssh` or
  served by the `nix-serve` Perl script.

A *binary cache* is a specialized Nix store whose metadata and
signatures are kept in `.narinfo` files rather than in the Nix
database. Examples of binary caches include S3 buckets and the
[NixOS binary cache](https://cache.nixos.org).

A *substituter* is a store other than `/nix/store` from which Nix will
copy a store path instead of building it.  Nix will not copy a store
path from a remote store unless one of the following is true:

- the store object is signed by one of the `trusted-public-keys`
- the substituter is in the `trusted-substituters` list
- the `no-require-sigs` option has been set to disable signature checking
- the store object is a derivation
- the store object is the realisation of a fixed-output derivation
