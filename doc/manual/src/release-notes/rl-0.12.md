# Release 0.12 (2008-11-20)

  - Nix no longer uses Berkeley DB to store Nix store metadata. The
    principal advantages of the new storage scheme are: it works
    properly over decent implementations of NFS (allowing Nix stores to
    be shared between multiple machines); no recovery is needed when a
    Nix process crashes; no write access is needed for read-only
    operations; no more running out of Berkeley DB locks on certain
    operations.
    
    You still need to compile Nix with Berkeley DB support if you want
    Nix to automatically convert your old Nix store to the new schema.
    If you don’t need this, you can build Nix with the `configure`
    option `--disable-old-db-compat`.
    
    After the automatic conversion to the new schema, you can delete the
    old Berkeley DB files:
    
        $ cd /nix/var/nix/db
        $ rm __db* log.* derivers references referrers reserved validpaths DB_CONFIG
    
    The new metadata is stored in the directories `/nix/var/nix/db/info`
    and `/nix/var/nix/db/referrer`. Though the metadata is stored in
    human-readable plain-text files, they are not intended to be
    human-editable, as Nix is rather strict about the format.
    
    The new storage schema may or may not require less disk space than
    the Berkeley DB environment, mostly depending on the cluster size of
    your file system. With 1 KiB clusters (which seems to be the `ext3`
    default nowadays) it usually takes up much less space.

  - There is a new substituter that copies paths directly from other
    (remote) Nix stores mounted somewhere in the filesystem. For
    instance, you can speed up an installation by mounting some remote
    Nix store that already has the packages in question via NFS or
    `sshfs`. The environment variable `NIX_OTHER_STORES` specifies the
    locations of the remote Nix directories, e.g. `/mnt/remote-fs/nix`.

  - New `nix-store` operations `--dump-db` and `--load-db` to dump and
    reload the Nix database.

  - The garbage collector has a number of new options to allow only some
    of the garbage to be deleted. The option `--max-freed N` tells the
    collector to stop after at least *N* bytes have been deleted. The
    option `--max-links
            N` tells it to stop after the link count on `/nix/store` has dropped
    below *N*. This is useful for very large Nix stores on filesystems
    with a 32000 subdirectories limit (like `ext3`). The option
    `--use-atime` causes store paths to be deleted in order of ascending
    last access time. This allows non-recently used stuff to be deleted.
    The option `--max-atime time` specifies an upper limit to the last
    accessed time of paths that may be deleted. For instance,
    
    ``` 
        $ nix-store --gc -v --max-atime $(date +%s -d "2 months ago")
    ```
    
    deletes everything that hasn’t been accessed in two months.

  - `nix-env` now uses optimistic profile locking when performing an
    operation like installing or upgrading, instead of setting an
    exclusive lock on the profile. This allows multiple `nix-env -i / -u
    / -e` operations on the same profile in parallel. If a `nix-env`
    operation sees at the end that the profile was changed in the
    meantime by another process, it will just restart. This is generally
    cheap because the build results are still in the Nix store.

  - The option `--dry-run` is now supported by `nix-store -r` and
    `nix-build`.

  - The information previously shown by `--dry-run` (i.e., which
    derivations will be built and which paths will be substituted) is
    now always shown by `nix-env`, `nix-store -r` and `nix-build`. The
    total download size of substitutable paths is now also shown. For
    instance, a build will show something like
    
        the following derivations will be built:
          /nix/store/129sbxnk5n466zg6r1qmq1xjv9zymyy7-activate-configuration.sh.drv
          /nix/store/7mzy971rdm8l566ch8hgxaf89x7lr7ik-upstart-jobs.drv
          ...
        the following paths will be downloaded/copied (30.02 MiB):
          /nix/store/4m8pvgy2dcjgppf5b4cj5l6wyshjhalj-samba-3.2.4
          /nix/store/7h1kwcj29ip8vk26rhmx6bfjraxp0g4l-libunwind-0.98.6
          ...

  - Language features:
    
      - @-patterns as in Haskell. For instance, in a function definition
        
            f = args @ {x, y, z}: ...;
        
        `args` refers to the argument as a whole, which is further
        pattern-matched against the attribute set pattern `{x, y, z}`.
    
      - “`...`” (ellipsis) patterns. An attribute set pattern can now
        say `...` at the end of the attribute name list to specify that
        the function takes *at least* the listed attributes, while
        ignoring additional attributes. For instance,
        
            {stdenv, fetchurl, fuse, ...}: ...
        
        defines a function that accepts any attribute set that includes
        at least the three listed attributes.
    
      - New primops: `builtins.parseDrvName` (split a package name
        string like `"nix-0.12pre12876"` into its name and version
        components, e.g. `"nix"` and `"0.12pre12876"`),
        `builtins.compareVersions` (compare two version strings using
        the same algorithm that `nix-env` uses), `builtins.length`
        (efficiently compute the length of a list), `builtins.mul`
        (integer multiplication), `builtins.div` (integer division).

  - `nix-prefetch-url` now supports `mirror://` URLs, provided that the
    environment variable `NIXPKGS_ALL` points at a Nixpkgs tree.

  - Removed the commands `nix-pack-closure` and `nix-unpack-closure`.
    You can do almost the same thing but much more efficiently by doing
    `nix-store --export
            $(nix-store -qR paths) > closure` and `nix-store --import <
            closure`.

  - Lots of bug fixes, including a big performance bug in the handling
    of `with`-expressions.
