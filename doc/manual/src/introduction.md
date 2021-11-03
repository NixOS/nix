# Introduction

Nix is a _purely functional package manager_.  This means that it
treats packages like values in purely functional programming languages
such as Haskell — they are built by functions that don’t have
side-effects, and they never change after they have been built.  Nix
stores packages in the _Nix store_, usually the directory
`/nix/store`, where each package has its own unique subdirectory such
as

    /nix/store/b6gvzjyb2pg0kjfwrjmg1vfhh54ad73z-firefox-33.1/

where `b6gvzjyb2pg0…` is a unique identifier for the package that
captures all its dependencies (it’s a cryptographic hash of the
package’s build dependency graph).  This enables many powerful
features.

## Multiple versions

You can have multiple versions or variants of a package
installed at the same time.  This is especially important when
different applications have dependencies on different versions of the
same package — it prevents the “DLL hell”.  Because of the hashing
scheme, different versions of a package end up in different paths in
the Nix store, so they don’t interfere with each other.

An important consequence is that operations like upgrading or
uninstalling an application cannot break other applications, since
these operations never “destructively” update or delete files that are
used by other packages.

## Complete dependencies

Nix helps you make sure that package dependency specifications are
complete.  In general, when you’re making a package for a package
management system like RPM, you have to specify for each package what
its dependencies are, but there are no guarantees that this
specification is complete.  If you forget a dependency, then the
package will build and work correctly on _your_ machine if you have
the dependency installed, but not on the end user's machine if it's
not there.

Since Nix on the other hand doesn’t install packages in “global”
locations like `/usr/bin` but in package-specific directories, the
risk of incomplete dependencies is greatly reduced.  This is because
tools such as compilers don’t search in per-packages directories such
as `/nix/store/5lbfaxb722zp…-openssl-0.9.8d/include`, so if a package
builds correctly on your system, this is because you specified the
dependency explicitly. This takes care of the build-time dependencies.

Once a package is built, runtime dependencies are found by scanning
binaries for the hash parts of Nix store paths (such as `r8vvq9kq…`).
This sounds risky, but it works extremely well.

## Multi-user support

Nix has multi-user support.  This means that non-privileged users can
securely install software.  Each user can have a different _profile_,
a set of packages in the Nix store that appear in the user’s `PATH`.
If a user installs a package that another user has already installed
previously, the package won’t be built or downloaded a second time.
At the same time, it is not possible for one user to inject a Trojan
horse into a package that might be used by another user.

## Atomic upgrades and rollbacks

Since package management operations never overwrite packages in the
Nix store but just add new versions in different paths, they are
_atomic_.  So during a package upgrade, there is no time window in
which the package has some files from the old version and some files
from the new version — which would be bad because a program might well
crash if it’s started during that period.

And since packages aren’t overwritten, the old versions are still
there after an upgrade.  This means that you can _roll back_ to the
old version:

```console
$ nix-env --upgrade some-packages
$ nix-env --rollback
```

## Garbage collection

When you uninstall a package like this…

```console
$ nix-env --uninstall firefox
```

the package isn’t deleted from the system right away (after all, you
might want to do a rollback, or it might be in the profiles of other
users).  Instead, unused packages can be deleted safely by running the
_garbage collector_:

```console
$ nix-collect-garbage
```

This deletes all packages that aren’t in use by any user profile or by
a currently running program.

## Functional package language

Packages are built from _Nix expressions_, which is a simple
functional language.  A Nix expression describes everything that goes
into a package build action (a “derivation”): other packages, sources,
the build script, environment variables for the build script, etc.
Nix tries very hard to ensure that Nix expressions are
_deterministic_: building a Nix expression twice should yield the same
result.

Because it’s a functional language, it’s easy to support
building variants of a package: turn the Nix expression into a
function and call it any number of times with the appropriate
arguments.  Due to the hashing scheme, variants don’t conflict with
each other in the Nix store.

## Transparent source/binary deployment

Nix expressions generally describe how to build a package from
source, so an installation action like

```console
$ nix-env --install firefox
```

_could_ cause quite a bit of build activity, as not only Firefox but
also all its dependencies (all the way up to the C library and the
compiler) would have to be built, at least if they are not already in the
Nix store.  This is a _source deployment model_.  For most users,
building from source is not very pleasant as it takes far too long.
However, Nix can automatically skip building from source and instead
use a _binary cache_, a web server that provides pre-built
binaries. For instance, when asked to build
`/nix/store/b6gvzjyb2pg0…-firefox-33.1` from source, Nix would first
check if the file `https://cache.nixos.org/b6gvzjyb2pg0….narinfo`
exists, and if so, fetch the pre-built binary referenced from there;
otherwise, it would fall back to building from source.

## Nix Packages collection

We provide a large set of Nix expressions containing hundreds of
existing Unix packages, the _Nix Packages collection_ (Nixpkgs).

## Managing build environments

Nix is extremely useful for developers as it makes it easy to
automatically set up the build environment for a package. Given a Nix
expression that describes the dependencies of your package, the
command `nix-shell` will build or download those dependencies if
they’re not already in your Nix store, and then start a Bash shell in
which all necessary environment variables (such as compiler search
paths) are set.

For example, the following command gets all dependencies of the
Pan newsreader, as described by [its
Nix expression](https://github.com/NixOS/nixpkgs/blob/master/pkgs/applications/networking/newsreaders/pan/default.nix):

```console
$ nix-shell '<nixpkgs>' -A pan
```

You’re then dropped into a shell where you can edit, build and test
the package:

```console
[nix-shell]$ unpackPhase
[nix-shell]$ cd pan-*
[nix-shell]$ configurePhase
[nix-shell]$ buildPhase
[nix-shell]$ ./pan/gui/pan
```

## Portability

Nix runs on Linux and macOS.

## NixOS

NixOS is a Linux distribution based on Nix.  It uses Nix not just for
package management but also to manage the system configuration (e.g.,
to build configuration files in `/etc`).  This means, among other
things, that it is easy to roll back the entire configuration of the
system to an earlier state.  Also, users can install software without
root privileges.  For more information and downloads, see the [NixOS
homepage](https://nixos.org/).

## License

Nix is released under the terms of the [GNU LGPLv2.1 or (at your
option) any later
version](http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html).
