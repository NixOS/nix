# Garbage Collector Roots

The roots of the garbage collector are all store paths to which there
are symlinks in the directory `prefix/nix/var/nix/gcroots`. For
instance, the following command makes the path
`/nix/store/d718ef...-foo` a root of the collector:

```console
$ ln -s /nix/store/d718ef...-foo /nix/var/nix/gcroots/bar
```

That is, after this command, the garbage collector will not remove
`/nix/store/d718ef...-foo` or any of its dependencies.

Subdirectories of `prefix/nix/var/nix/gcroots` are searched
recursively. Symlinks to store paths count as roots. Symlinks to
non-store paths are ignored, unless the non-store path is itself a
symlink to a store path.