R"(

**Store URL format**: `local-overlay`

This store type is a variation of the [local store] designed to leverage Linux's [Overlay Filesystem](https://docs.kernel.org/filesystems/overlayfs.html) (OverlayFS for short).
Just as OverlayFS combines a lower and upper filesystem by treating the upper one as a patch against the lower, the local overlay store combines a lower store with an upper almost-[local store].
("almost" because while the upper fileystems for OverlayFS is valid on its own, the upper almost-store is not a valid local store on its own because some references will dangle.)
To use this store, you will first need to configure an OverlayFS mountpoint [appropriately](#example-filesystem-layout) as Nix will not do this for you (though it will verify the mountpoint is configured correctly).

### Conceptual parts of a local overlay store

*This is a more abstract/conceptual description of the parts of a layered store, an authoritative reference.
For more "practical" instructions, see the worked-out example in the next subsection.*

The parts of a local overlay store are as follows:

- **Lower store**:

  > Specified with the [`lower-store`](#store-experimental-local-overlay-store-lower-store) setting.

  This is any store implementation that includes a store directory as part of the native operating system filesystem.
  For example, this could be a [local store], [local daemon store], or even another local overlay store.

  The local overlay store never tries to modify the lower store in any way.
  Something else could modify the lower store, but there are restrictions on this
  Nix itself requires that this store only grow, and not change in other ways.
  For example, new store objects can be added, but deleting or modifying store objects is not allowed in general, because that will confuse and corrupt any local overlay store using those objects.
  (In addition, the underlying filesystem overlay mechanism may impose additional restrictions, see below.)

  The lower store must not change while it is mounted as part of an overlay store.
  To ensure it does not, you might want to mount the store directory read-only (which then requires the [read-only] parameter to be set to `true`).

  - **Lower store directory**:

    > Specified with `lower-store.real` setting.

    This is the directory used/exposed by the lower store.

    As specified above, Nix requires the local store can only grow not change in other ways.
    Linux's OverlayFS in addition imposes the further requirement that this directory cannot change at all.
    That means that, while any local overlay store exists that is using this store as a lower store, this directory must not change.

  - **Lower metadata source**:

    > Not directly specified.
    > A consequence of the `lower-store` setting, depending on the type of lower store chosen.

    This is abstract, just some way to read the metadata of lower store [store objects][store object].
    For example it could be a SQLite database (for the [local store]), or a socket connection (for the [local daemon store]).

    This need not be writable.
    As stated above a local overlay store never tries to modify its lower store.
    The lower store's metadata is considered part of the lower store, just as the store's [file system objects][file system object] that appear in the store directory are.

- **Upper almost-store**:

  > Not directly specified.
  > Instead the constituent parts are independently specified as described below.

  This is almost but not quite just a [local store].
  That is because taken in isolation, not as part of a local overlay store, by itself, it would appear corrupted.
  But combined with everything else as part of an overlay local store, it is valid.

  - **Upper layer directory**:

    > Specified with [`upper-layer`](#store-experimental-local-overlay-store-upper-layer) setting.

    This contains additional [store objects][store object]
    (or, strictly speaking, their [file system objects][file system object] that the local overlay store will extend the lower store with).

  - **Upper store directory**:

    > Specified with the [`real`](#store-experimental-local-overlay-store-real) setting.
    > This the same as the base local store setting, and can also be indirectly specified with the [`root`](#store-experimental-local-overlay-store-root) setting.

    This contains all the store objects from each of the two directories.

    The lower store directory and upper layer directory are combined via OverlayFS to create this directory.
    Nix doesn't do this itself, because it typically wouldn't have the permissions to do so, so it is the responsibility of the user to set this up first.
    Nix can, however, optionally check that that the OverlayFS mount settings appear as expected, matching Nix's own settings.

  - **Upper SQLite database**:

    > Not directly specified.
    > The location of the database instead depends on the [`state`](#store-experimental-local-overlay-store-state) setting.
    > It is is always `${state}/db`.

    This contains the metadata of all of the upper layer [store objects][store object] (everything beyond their file system objects), and also duplicate copies of some lower layer store object's metadta.
    The duplication is so the metadata for the [closure](@docroot@/glossary.md#gloss-closure) of upper layer [store objects][store object] can be found entirely within the upper layer.
    (This allows us to use the same SQL Schema as the [local store]'s SQLite database, as foreign keys in that schema enforce closure metadata to be self-contained in this way.)

[file system object]: @docroot@/store/file-system-object.md
[store object]: @docroot@/store/store-object.md


### Example filesystem layout

Here is a worked out example of usage, following the concepts in the previous section.

Say we have the following paths:

- `/mnt/example/merged-store/nix/store`

- `/mnt/example/store-a/nix/store`

- `/mnt/example/store-b`

Then the following store URI can be used to access a local-overlay store at `/mnt/example/merged-store`:

```
local-overlay://?root=/mnt/example/merged-store&lower-store=/mnt/example/store-a&upper-layer=/mnt/example/store-b
```

The lower store directory is located at `/mnt/example/store-a/nix/store`, while the upper layer is at `/mnt/example/store-b`.

Before accessing the overlay store you will need to ensure the OverlayFS mount is set up correctly:

```shell
mount -t overlay overlay \
  -o lowerdir="/mnt/example/store-a/nix/store" \
  -o upperdir="/mnt/example/store-b" \
  -o workdir="/mnt/example/workdir" \
  "/mnt/example/merged-store/nix/store"
```

Note that OverlayFS requires `/mnt/example/workdir` to be on the same volume as the `upperdir`.

By default, Nix will check that the mountpoint as been set up correctly and fail with an error if it has not.
You can override this behaviour by passing [`check-mount=false`](#store-experimental-local-overlay-store-check-mount) if you need to.

)"
