# Welcome to Nix

# Where Nix comes from

Nix was created by Eelco Dolstra and developed as the subject of his PhD thesis [The Purely Functional Software Deployment Model](https://edolstra.github.io/pubs/phd-thesis.pdf), published 2006.
Over the years, many others have joined the effort to make software work reliably, now and in the long run.
Today, a [world-wide developer community](https://nixos.org/) contributes to Nix and the ecosystem that has grown around it.

# How Nix works

> The name *Nix* is derived from the Dutch word *niks*, meaning *nothing*;
> build actions do not see anything that has not been explicitly declared as an input.
>
> — [Nix: A Safe and Policy-Free System for Software Deployment](https://edolstra.github.io/pubs/nspfssd-lisa2004-final.pdf), LISA XVIII, 2004

Nix primarily deals with making sure that files are complete and available when processes need them.
This is accomplished by two measures:
- All files and their relations are specified by cryptographic hash in the Nix store, and are made immutable.
  Files are only deleted when nothing refers to them any more.
  This guarantees that a successful program run can be repeated, because nothing will change or disappear inadvertently.
- Processes are executed in a clean environment, where they can only access what is explictly allowed.
  This prevents obtaining useful results without specifyig all dependencies.

Nix has become known as the *purely functional package manager*, because this approach to ensure repeatability allows escaping from [dependency hell](https://en.wikipedia.org/wiki/Dependency_hell) and taming the complexity of the software ecosystem.
It is grown an ecosystem of exceptionally powerful tools – including Nixpkgs, , and NixOS, a Linux distribution that can be configured fully declaratively, with unmatched flexibility.

At the heart of this is the [Nix store](./store/index.md).
It keeps immutable file system objects, and tracks dependencies between them.
It can also execute sandboxed processes and efficiently copy bundled dependencies across machines in a way that preverves correctness.

Nix comes with a purely functional programming language to describe and compose these computations, and succinctly refer to their inputs and outputs: the [Nix language](./language/index.html).

# What Nix can do

Nix enforces *complete, exact* dependencies whenever software under its control is run.
This makes possible unique features without compromising on efficiency.

## Multiple software versions side by side

You can have multiple versions or variants of a package installed at the same time.
Different files always end up on different paths in the Nix store, so they don’t interfere with each other.
This circumvents the “dependency hell”.

An important consequence is that upgrading or uninstalling an application cannot break other applications, since these operations never “destructively” update or delete files that are used otherwise.

## Multi-user support

Nix has multi-user support.
This means that non-privileged users can securely install software.
Each user can have a different _profile_, a set of programs in the Nix store that appear in the user’s `$PATH`.
If a user installs a package that another user has already installed previously, the package won’t be built or downloaded a second time.
At the same time, it is not possible for one user to inject malicious code into a package another user may rely on.

## Atomic upgrades and rollbacks

Since files in the Nix store are never changed, new versions are added to different paths, and environments are constructed from symlinks to these paths.

Swapping symlinks to paths is _atomic_.
With Nix, during a software upgrade, there is no time window in which a package has some files from the old version and some files from the new version.
This eliminates inconsistencies by construction.

And since files aren’t overwritten, the old versions are still there after an upgrade.
This means that you can instantly _roll back_ to an old version.

## Garbage collection

Files are’t deleted from the system right away — after all, you might want to do a rollback, or they might be in the profiles of other users.
Instead, unused packages can be deleted safely by running the _garbage collector_.

This deletes all packages that aren’t in use by a currently running program or otherwise.

## Transparent source/binary deployment

Nix expressions generally describe how to build software from source, starting from some initial binary that is taken for granted.
This is a _source deployment model_.

But since outputs are cached, building is only required if the output in question are not already in the Nix store.
Nix can also automatically skip building from source and instead use a _binary cache_, a web server that provides pre-built binaries.
This is a _binary deployment model_.

[Hydra](https://hydra.nixos.org/), the Nix build cluster, has been producing binary artifacts and making them available in the [public cache](http://cache.nixos.org/) for many years.
The cache allows running software going back as far as 2010, especially where sources cannot be obtained otherwise any more.

Nix also makes it easy to set up your own binary cache.

## Distributed builds

Nix can **transparently delegate tasks to remote machines**, for example to build on different architectures or operating systems, or run many builds in parallel.
These machines can share the same cache.

Setting up your own remote build machine only requires installing Nix (or NixOS), registering SSH keys, and configuring the client when to use it.

## Functional configuration language

Which artifacts Nix produces is traditionally specified with _Nix expressions_.

A Nix expression describes everything that goes into a “derivation”:
the executable to run, input files and environment variables it can read, and command line arguments.

Nix tries very hard to ensure that **Nix expressions are deterministic**:
both evaluating the Nix expression and running an executable configured that way  should yield the same result every time.

Because **it’s a purely functional programming language**, it allows expressing highly complex compositions and their variants succinctly.
Since unique files have unique file system paths, variants won’t conflict with each other in the Nix store.

## Nixpkgs software distribution

[Nixpkgs](https://github.com/NixOS/nixpkgs) has become [the largest, most up-to-date free software repository in the world](https://repology.org/repositories/graphs).
It holds tens of thousands of working packages **built from source** and **instantly available in binary form**.
It offers mechanisms to **compose or customise software** according to your needs.

## NixOS Linux distribution

[NixOS](https://github.com/NixOS/nixpkgs/tree/master/nixos) is a Linux distribution based on Nix and Nixpkgs, and allows you to **manage the entire system configuration** through the Nix language in a central configuration file.

This means, among other things, that it is easy to configure settings for all programs and services uniformly, and recreate them any time from a text file, by rolling back to an earlier state, deploying it directly to remote machines, or building containers and virtual machines efficienly.

## Cross-platform

Nix is written in C++.
It runs on Linux, macOS, and WSL, and is compiled for many common CPU architectures.

# How to use Nix

This is the reference manual for Nix: it documents the Nix store, the Nix language, the command line interface, and formats, protocols, and APIs Nix supports.
It is intended for looking up facts and precise details.
Here is the place where information about Nix is most likely to be correct.

Reading it “back to back” is certainly not the quickest or easiest way to learn how to use Nix.
But it is a reliable path to gain a solid understanding of the sytem.
Your feedback on any aspect of this manual is appreciated and will help us make it better.

If you want to get things done quickly, visit [nix.dev](https://nix.dev) for [beginner tutorials](https://nix.dev/tutorials/first-steps).

If you want to know how the full truth, get the [source code on GitHub](https://github.com/NixOS/nix).

## How to help

Check the [contributing guide](https://github.com/NixOS/nix) if you want to get involved with developing Nix.

# License

Nix is released under the terms of the [GNU LGPLv2.1 or (at your option) any later version](http://www.gnu.org/licenses/old-licenses/lgpl-2.1.html).

