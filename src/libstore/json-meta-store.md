R"(

**Store URL format**: `json-meta://?root=*root*`

This store type persists store objects on disk without an intervening daemon exactly like the the [Local Store] does.
However, instead of storing store object metadata in a SQLite database, it stores it in JSON files in the [`meta`](...) directory.

This is much less performant for many tasks, and not recommend for most users.
However, it does have some benefits regarding synchronization and contention.
For example, adding new store objects will not touch the JSON files for existing store objects in any way, whereas any change to the store at all with the [Local Store] will modify the SQLite database.
Thus, for certain obscure use-cases of broadcasting a store to a wide number of consumers, it may be advantageous to use a store of this type instead of a Local Store.

[Local Store]: ./local-store.md

)"
