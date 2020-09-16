# Garbage Collection

`nix-env` operations such as upgrades (`-u`) and uninstall (`-e`) never
actually delete packages from the system. All they do (as shown above)
is to create a new user environment that no longer contains symlinks to
the “deleted” packages.

Of course, since disk space is not infinite, unused packages should be
removed at some point. You can do this by running the Nix garbage
collector. It will remove from the Nix store any package not used
(directly or indirectly) by any generation of any profile.

Note however that as long as old generations reference a package, it
will not be deleted. After all, we wouldn’t be able to do a rollback
otherwise. So in order for garbage collection to be effective, you
should also delete (some) old generations. Of course, this should only
be done if you are certain that you will not need to roll back.

To delete all old (non-current) generations of your current profile:

```console
$ nix-env --delete-generations old
```

Instead of `old` you can also specify a list of generations, e.g.,

```console
$ nix-env --delete-generations 10 11 14
```

To delete all generations older than a specified number of days (except
the current generation), use the `d` suffix. For example,

```console
$ nix-env --delete-generations 14d
```

deletes all generations older than two weeks.

After removing appropriate old generations you can run the garbage
collector as follows:

```console
$ nix-store --gc
```

The behaviour of the gargage collector is affected by the
`keep-derivations` (default: true) and `keep-outputs` (default: false)
options in the Nix configuration file. The defaults will ensure that all
derivations that are build-time dependencies of garbage collector roots
will be kept and that all output paths that are runtime dependencies
will be kept as well. All other derivations or paths will be collected.
(This is usually what you want, but while you are developing it may make
sense to keep outputs to ensure that rebuild times are quick.) If you
are feeling uncertain, you can also first view what files would be
deleted:

```console
$ nix-store --gc --print-dead
```

Likewise, the option `--print-live` will show the paths that *won’t* be
deleted.

There is also a convenient little utility `nix-collect-garbage`, which
when invoked with the `-d` (`--delete-old`) switch deletes all old
generations of all profiles in `/nix/var/nix/profiles`. So

```console
$ nix-collect-garbage -d
```

is a quick and easy way to clean up your system.
