## Advanced Topic: store object relocatability

Now that we know the fundamentals of the design of the Nix store, let's explore one consequence of that design: the question when it is permissable to relocate a store object to a store with a different mount point.

Recall from the section on [store paths](./store-paths.md), concrete store paths look like `<store-dir>/<hash>-<name>`.

~~The two final restrictions of the previous section yield an alternative view of the same information.~~
Rather than associating store dirs with the references, we can say a store object itself has a store dir if and only if it has at least one reference.

This corresponds to the observation that a store object with references, i.e. with a store directory under this interpretation, is confined to stores sharing that same store directory, but a store object without any references, i.e. thus without a store directory, can exist in any store.

Lastly, this illustrates the purpose of tracking self references.
Store objects without self-references or other references are relocatable, while store paths with self-references aren't.
This is used to tell apart e.g. source code which can be stored anywhere, and pesky non-reloctable executables which assume they are installed to a certain path.
\[The default method of calculating references by scanning for store paths handles these two example cases surprisingly well.\]
