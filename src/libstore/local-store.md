R"(

**Store URL format**: `local`, *root*

This store type accesses a Nix store in the local filesystem directly
(i.e. not via the Nix daemon). *root* is an absolute path that denotes
the "root" of the filesystem; other directories such as the Nix store
directory (as denoted by the `store` setting) are interpreted relative
to *root*. The store pseudo-URL `local` denotes a store that uses `/`
as its root directory.

A store that uses a *root* other than `/` is called a *chroot
store*. With such stores, the store directory is "logically" still
`/nix/store`, so programs stored in them can only be built and
executed by `chroot`-ing into *root*. Chroot stores only support
building and running on Linux when mount and user namespaces are
enabled.

For example, the following uses `/tmp/root` as the chroot environment
to build or download `nixpkgs#hello` and then execute it:

```console
# nix run --store /tmp/root nixpkgs#hello
Hello, world!
```

Here, the "physical" store location is `/tmp/root/nix/store`, and
Nix's store metadata is in `/tmp/root/nix/var/nix/db`.

It is also possible, but not recommended, to change the "logical"
location of the Nix store from its default of `/nix/store`. This makes
it impossible to use default substituters such as
`https://cache.nixos.org/`, and thus you may have to build everything
locally. Here is an example:

```console
# nix build --store 'local?store=/tmp/my-nix/store&state=/tmp/my-nix/state&log=/tmp/my-nix/log' nixpkgs#hello
```

)"
