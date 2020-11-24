# Store Entries

File system data in Nix is organized into *store entries*.
A store entry is the combination of

  - some file system data
  - references to store entries

## File system data

Nix supports the a similar model of the file system as Git.
Namely, every file system object falls into these three cases:

 - File: arbitrary data

 - Directory: mapping of names to child file system objects.
   File children additionally have an executable flag.

 - Symlink: may point anywhere.
   In particular, Symlinks that do not point within the containing file system data or that of another store entry referenced by the containing store entry are allowed, but might not function as intended.

A bare file as the "root" file system object is allowed.
Note that there is no containing directory object to store its executable bit; it's deemed non-executable by default.

## References

Store entries can refer to both other store entries and themselves.

Store references are normally calculated by scanning the file system data for store paths when a new store entry is created, but this isn't mandatory, as store entries are allowed to have references that don't correspond to contained store paths, and contained store paths that don't correspond to references.

The references themselves need not be store paths per-se (this is an implementation detail of the store).
But, like rendered store paths (see next section) in addition to identifying store entries they must also identify the store directory of the store(s) that contain those store entries.
That said, all the references of the store entry must agree on a store dir.
Also the store directory of the references must equal that of any store which contains the store entry doing the referencing.

## Relocatability

The two final restrictions of the previous section yield an alternative of view of the same information.
Rather than associating store dirs with the references, we can say a store entry itself has a store dir if and only if it has at least once reference.

This corresponds to the observation that a store entry with references, i.e. with a store directory under this interpretation, is confined to stores sharing that same store directory, but a store entry without any references, i.e. thus without a store directory, can exist in any store.

Lastly, this illustrates the purpose of tracking self references.
Store entries without self-references or other references are relocatable, while store paths with self-references aren't.
This is used to tell apart e.g. source code which can be stored anywhere, and pesky non-reloctable executables which assume they are installed to a certain path.
\[The default method of calculating references by scanning for store paths handles these two example cases surprisingly well.\]
