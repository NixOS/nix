# Building Nix from Source

Nix is built with [Meson](https://mesonbuild.com/).
It is broken up into multiple Meson packages, which are optionally combined in a single project using Meson's [subprojects](https://mesonbuild.com/Subprojects.html) feature.

There are no mandatory extra steps to the building process:
generic Meson installation instructions like [this](https://mesonbuild.com/Quick-guide.html#using-meson-as-a-distro-packager) should work.

The installation path can be specified by passing the `-Dprefix=prefix`
to `configure`. The default installation directory is `/usr/local`. You
can change this to any location you like. You must have write permission
to the *prefix* path.

Nix keeps its *store* (the place where packages are stored) in
`/nix/store` by default. This can be changed using
`-Dstore-dir=path`.

> **Warning**
>
> It is best *not* to change the Nix store from its default, since doing
> so makes it impossible to use pre-built binaries from the standard
> Nixpkgs channels — that is, all packages will need to be built from
> source.

Nix keeps state (such as its database and log files) in `/nix/var` by
default. This can be changed using `-Dlocalstatedir=path`.
