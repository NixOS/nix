# Basic Package Management

The main command for package management is
[`nix-env`](../command-ref/nix-env.md).  You can use it to install,
upgrade, and erase packages, and to query what packages are installed
or are available for installation.

In Nix, different users can have different “views” on the set of
installed applications. That is, there might be lots of applications
present on the system (possibly in many different versions), but users
can have a specific selection of those active — where “active” just
means that it appears in a directory in the user’s `PATH`. Such a view
on the set of installed applications is called a *user environment*,
which is just a directory tree consisting of symlinks to the files of
the active applications.

Components are installed from a set of *Nix expressions* that tell Nix
how to build those packages, including, if necessary, their
dependencies. There is a collection of Nix expressions called the
Nixpkgs package collection that contains packages ranging from basic
development stuff such as GCC and Glibc, to end-user applications like
Mozilla Firefox. (Nix is however not tied to the Nixpkgs package
collection; you could write your own Nix expressions based on Nixpkgs,
or completely new ones.)

You can manually download the latest version of Nixpkgs from
<https://github.com/NixOS/nixpkgs>. However, it’s much more
convenient to use the Nixpkgs [*channel*](channels.md), since it makes
it easy to stay up to date with new versions of Nixpkgs. Nixpkgs is
automatically added to your list of “subscribed” channels when you
install Nix. If this is not the case for some reason, you can add it
as follows:

```console
$ nix-channel --add https://nixos.org/channels/nixpkgs-unstable
$ nix-channel --update
```

> **Note**
> 
> On NixOS, you’re automatically subscribed to a NixOS channel
> corresponding to your NixOS major release (e.g.
> <http://nixos.org/channels/nixos-21.11>). A NixOS channel is identical
> to the Nixpkgs channel, except that it contains only Linux binaries
> and is updated only if a set of regression tests succeed.

You can view the set of available packages in Nixpkgs:

```console
$ nix-env -qaP
nixpkgs.aterm                       aterm-2.2
nixpkgs.bash                        bash-3.0
nixpkgs.binutils                    binutils-2.15
nixpkgs.bison                       bison-1.875d
nixpkgs.blackdown                   blackdown-1.4.2
nixpkgs.bzip2                       bzip2-1.0.2
…
```

The flag `-q` specifies a query operation, `-a` means that you want
to show the “available” (i.e., installable) packages, as opposed to the
installed packages, and `-P` prints the attribute paths that can be used
to unambiguously select a package for installation (listed in the first column).
If you downloaded Nixpkgs yourself, or if you checked it out from GitHub,
then you need to pass the path to your Nixpkgs tree using the `-f` flag:

```console
$ nix-env -qaPf /path/to/nixpkgs
aterm                               aterm-2.2
bash                                bash-3.0
…
```

where */path/to/nixpkgs* is where you’ve unpacked or checked out
Nixpkgs.

You can filter the packages by name:

```console
$ nix-env -qaP firefox
nixpkgs.firefox-esr                 firefox-91.3.0esr
nixpkgs.firefox                     firefox-94.0.1
```

and using regular expressions:

```console
$ nix-env -qaP 'firefox.*'
```

It is also possible to see the *status* of available packages, i.e.,
whether they are installed into the user environment and/or present in
the system:

```console
$ nix-env -qaPs
…
-PS  nixpkgs.bash                bash-3.0
--S  nixpkgs.binutils            binutils-2.15
IPS  nixpkgs.bison               bison-1.875d
…
```

The first character (`I`) indicates whether the package is installed in
your current user environment. The second (`P`) indicates whether it is
present on your system (in which case installing it into your user
environment would be a very quick operation). The last one (`S`)
indicates whether there is a so-called *substitute* for the package,
which is Nix’s mechanism for doing binary deployment. It just means that
Nix knows that it can fetch a pre-built package from somewhere
(typically a network server) instead of building it locally.

You can install a package using `nix-env -iA`. For instance,

```console
$ nix-env -iA nixpkgs.subversion
```

will install the package called `subversion` from `nixpkgs` channel (which is, of course, the
[Subversion version management system](http://subversion.tigris.org/)).

> **Note**
> 
> When you ask Nix to install a package, it will first try to get it in
> pre-compiled form from a *binary cache*. By default, Nix will use the
> binary cache <https://cache.nixos.org>; it contains binaries for most
> packages in Nixpkgs. Only if no binary is available in the binary
> cache, Nix will build the package from source. So if `nix-env
> -iA nixpkgs.subversion` results in Nix building stuff from source, then either
> the package is not built for your platform by the Nixpkgs build
> servers, or your version of Nixpkgs is too old or too new. For
> instance, if you have a very recent checkout of Nixpkgs, then the
> Nixpkgs build servers may not have had a chance to build everything
> and upload the resulting binaries to <https://cache.nixos.org>. The
> Nixpkgs channel is only updated after all binaries have been uploaded
> to the cache, so if you stick to the Nixpkgs channel (rather than
> using a Git checkout of the Nixpkgs tree), you will get binaries for
> most packages.

Naturally, packages can also be uninstalled. Unlike when installing, you will
need to use the derivation name (though the version part can be omitted),
instead of the attribute path, as `nix-env` does not record which attribute
was used for installing:

```console
$ nix-env -e subversion
```

Upgrading to a new version is just as easy. If you have a new release of
Nix Packages, you can do:

```console
$ nix-env -uA nixpkgs.subversion
```

This will *only* upgrade Subversion if there is a “newer” version in the
new set of Nix expressions, as defined by some pretty arbitrary rules
regarding ordering of version numbers (which generally do what you’d
expect of them). To just unconditionally replace Subversion with
whatever version is in the Nix expressions, use `-i` instead of `-u`;
`-i` will remove whatever version is already installed.

You can also upgrade all packages for which there are newer versions:

```console
$ nix-env -u
```

Sometimes it’s useful to be able to ask what `nix-env` would do, without
actually doing it. For instance, to find out what packages would be
upgraded by `nix-env -u`, you can do

```console
$ nix-env -u --dry-run
(dry run; not doing anything)
upgrading `libxslt-1.1.0' to `libxslt-1.1.10'
upgrading `graphviz-1.10' to `graphviz-1.12'
upgrading `coreutils-5.0' to `coreutils-5.2.1'
```
