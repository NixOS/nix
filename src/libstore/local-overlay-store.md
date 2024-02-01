R"(

**Store URL format**: `local-overlay`

This store type is a variation of the [local store] designed to leverage Linux's [Overlay Filesystem](https://docs.kernel.org/filesystems/overlayfs.html) (OverlayFS for short).
Just as OverlayFS combines a lower and upper filesystem by treating the upper one as a patch against the lower, the local overlay store combines a lower store with an upper almost [local store].
("almost" because while the upper fileystems for OverlayFS is valid on its own, the upper almost-store is not a valid local store on its own because some references will dangle.)
To use this store, you will first need to configure an OverlayFS mountpoint [appropriately](#example-filesystem-layout) as Nix will not do this for you (though it will verify the mountpoint is configured correctly).

### Parts of a local overlay store

The parts of a local overlay store are as follows:

- Lower store:

  This is any store implementation that includes a store directory as part of the native operating system filesystem.
  For example, this could be a [local store], [local daemon store], or even another local overlay store.

  The lower store must not change while it is mounted as part of an overlay store.
  To ensure it does not, you might want to mount the store directory read-only (which then requires the [read-only] parameter to be set to `true`).

  Specified with the `lower-store` setting.

  - Lower store directory.
    This is the directory used/exposed by the lower store.

    Specified with `lower-store.real` setting.

  - Lower abstract read-only metadata source.
    This is abstract, just some way to read the metadata of lower store [store objects](@docroot@/glossary.md#gloss-store-object).
    For example it could be a SQLite database (for the [local store]), or a socket connection (for the [local daemon store]).

- Upper almost-store:

  This is a [local store] that by itself would appear corrupted.
  But combined with everything else as part of an overlay local store, it is valid.

  - Upper layer directory.
    This contains additional [store objects]
    (or, strictly speaking, their [file system objects](#gloss-file-system-object))
    that the local overlay store will extend the lower store with.

    Specified with `upper-layer` setting.

  - Upper store directory
    The lower store directory and upper layer directory are combined via OverlayFS to create this directory.
    This contains all the store objects from each of the two directories.

    Specified with the `real` setting.

  - Upper SQLite database
    This contains the metadata of all of the upper layer [store objects]: everything beyond their file system objects, and also duplicate copies of some lower layer ones.
    The duplication is so the metadata for the [closure](@docroot@/glossary.md#gloss-closure) of upper layer [store objects] can entirely be found in the upper layer.
    This allows us to use the same SQL Schema as the [local store]'s SQLite database, as foreign keys in that schema enforce closure metadata to be self-contained in this way.

    Specified with the `state` setting, is always `${state}/db`.


### Example filesystem layout

Say we have the following paths:

- `/mnt/example/merged-store/nix/store`

- `/mnt/example/store-a/nix/store`

- `/mnt/example/store-b`


Then the following store URI can be used to access a local-overlay store at `/mnt/example/merged-store`:

```
  local-overlay://?root=/mnt/example/merged-store&lower-store=/mnt/example/store-a&upper-layer=/mnt/example/store-b
```

The lower store is located at `/mnt/example/store-a/nix/store`, while the upper layer is at `/mnt/example/store-b`.

Before accessing the overlay store you will need to ensure the OverlayFS mount is set up correctly:

```
  mount -t overlay overlay \
    -o lowerdir="/mnt/example/store-a/nix/store" \
    -o upperdir="/mnt/example/store-b" \
    -o workdir="/mnt/example/workdir" \
    "/mnt/example/merged-store/nix/store" \
```

Note that OverlayFS requires `/mnt/example/workdir` to be on the same volume as the `upperdir`.

By default, Nix will check that the mountpoint as been set up correctly and fail with an error if it has not.
You can override this behaviour by passing [`check-mount=false`](#store-experimental-local-overlay-store-check-mount) if you need to.




)"
