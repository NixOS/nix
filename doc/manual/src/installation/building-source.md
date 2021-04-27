# Building Nix from Source

After unpacking or checking out the Nix sources, issue the following
commands:

```console
$ ./configure options...
$ make
$ make install
```

Nix requires GNU Make so you may need to invoke `gmake` instead.

When building from the Git repository, these should be preceded by the
command:

```console
$ ./bootstrap.sh
```

The installation path can be specified by passing the `--prefix=prefix`
to `configure`. The default installation directory is `/usr/local`. You
can change this to any location you like. You must have write permission
to the *prefix* path.

Nix keeps its *store* (the place where packages are stored) in
`/nix/store` by default. This can be changed using
`--with-store-dir=path`.

> **Warning**
> 
> It is best *not* to change the Nix store from its default, since doing
> so makes it impossible to use pre-built binaries from the standard
> Nixpkgs channels — that is, all packages will need to be built from
> source.

Nix keeps state (such as its database and log files) in `/nix/var` by
default. This can be changed using `--localstatedir=path`.
