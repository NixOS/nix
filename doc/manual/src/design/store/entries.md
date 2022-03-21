# Store Entries

File system data in Nix is organized into *store entries*.
A store entry is the combination of

  - some file system data
  - references to store entries

## File system data

The nix store uses a simple filesystem model, similar to the one Git uses.
In particular, every file system object falls into these three cases:

 - File: arbitrary data

 - Directory: mapping of names to child file system objects.
   File children additionally have an executable flag.

 - Symlink: may point anywhere.
   In particular, Symlinks that do not point within the containing file system data or that of another store entry referenced by the containing store entry are allowed, but might not function as intended.

A bare file as the "root" file system object is allowed.
Note that it cannot be executable, though.
This is a consequence of the executable flags being part of the child entries of the directory, rather than the child files themselves.
A root file has no parent directory; so there is no child entry about the root file, and therefore no executable flag for it.
Without a flag saying which, whether root files are executable or non-executable by default must be decided by convention, and the choice of Nix (and git) is to make them non-executable.

## References

Store entries can refer to both other store entries and themselves.

References are normally calculated by scanning the file system data for store paths (which we describe in the next section) referring to store entries.
For now, it suffices to say that a store path is a string encoding of a reference to a store paths, and therefore it is something that we can search for in the contents of files, and thus in store entries by searching in all their files.   
When we get to building in a future section, this process will be described in precise detail.

However, scanning for references is not mandatory.
Store entries are allowed to have official references that *don't* correspond to store paths contained in their contents,
and they are also allowed to *not* have references that *do* correspond to store paths contained in their store.
Taken together, this means there is no actual rule relating the store paths contained in the contents to the store paths deemed references.

This is why it's its necessary for correctness, and not just performance, that Nix remember the references of each store entry, rather than try to recompute them on the fly by scanning their contents.

The references themselves need not be store paths per-se (this is an implementation detail of the store).
But, like rendered store paths (see next section) in addition to identifying store entries they must also identify the store directory of the store(s) that contain those store entries.
That said, all the references of the store entry must agree on a store dir.
Also the store directory of the references must equal that of any store which contains the store entry doing the referencing.

## Relocatability

The two final restrictions of the previous section yield an alternative view of the same information.
Rather than associating store dirs with the references, we can say a store entry itself has a store dir if and only if it has at least one reference.

This corresponds to the observation that a store entry with references, i.e. with a store directory under this interpretation, is confined to stores sharing that same store directory, but a store entry without any references, i.e. thus without a store directory, can exist in any store.

Lastly, this illustrates the purpose of tracking self references.
Store entries without self-references or other references are relocatable, while store paths with self-references aren't.
This is used to tell apart e.g. source code which can be stored anywhere, and pesky non-reloctable executables which assume they are installed to a certain path.
\[The default method of calculating references by scanning for store paths handles these two example cases surprisingly well.\]
