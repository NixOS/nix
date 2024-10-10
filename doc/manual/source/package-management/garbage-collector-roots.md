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

Subdirectories of `prefix/nix/var/nix/gcroots` are also searched for
symlinks. Symlinks to non-store paths are followed and searched for
roots, but symlinks to non-store paths *inside* the paths reached in
that way are not followed to prevent infinite recursion.
