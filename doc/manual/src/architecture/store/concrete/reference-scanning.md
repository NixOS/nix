# Reference scanning

As discussed in a previous section, a store object is made up of file system objects and references to other store objects.

Suppose we are trying to create a new store object that *ought* to have references, but we only have plain file system data.
For example, imagine the file system data is a "shared library" (so, dll, dylib) that depends on other shared libraries, and those other shared libraries are already inside other store objects.
How might we do this?

One way would be to have the Nix user manually write down this information.
Another way would be to teach Nix to parse those files' headers and convert the "domain specific" dependency information within them.
Both of these are a huge amount of work, both initially and as ongoing maintenance, and therefore not very attractive options.

Nix instead opts to merely scan the file for store paths without making any attempt to parse it.
This "simple, stupid" approach actually works quite well most of the time, and imposes very little burden on the end user.

## Why it works

Store objects are, as previously mentioned, mounted at `<store-dir>/<hash>-<name>` when they are exposed to the file system.
This is the textual form of a [store path](./paths.md).
When this happens depends on the type of store, but it *must* happen during building, as regular Unix processes are being run.
Those paths contain hashes, and hashes are not guessable.
As such, build results *must* store those hashes if the programs inside them wish to refer to those store objects without the aid of extra information when they are run.

The hashes could be encrypted or compressed, but regular software doesn't need to do this.
The result is that just scanning for hashes works quite well!

## How it works

Hashes are scanned for, not entire store paths.
Thus, Nix would look for, e.g.,
```
b6gvzjyb2pg0kjfwrjmg1vfhh54ad73z
```
not
```
/nix/store/b6gvzjyb2pg0kjfwrjmg1vfhh54ad73z-firefox-33.1
```

## When it happens

Scanning happens when store objects are created that could refer to other store paths.
When source code is added, references are prohibited by fiat, and thus no scanning is needed.
Build results can refer to other objects, so scanning does happen at the end of a build.

## Exceptions

Note that just because Nix *can* scan references doesn't mean that it *must* scan for references.

There is no problem with a store object having an "extra" reference that doesn't correspond to a hash inside the dependency.
The store object will have an extra dependency that it doesn't need, but that is fine.
It is a case adjacent to that of a hash occurring in some obscure location within the store object that is never read.

[Drv files](./drvs/drvs.md), discussed next, might also contain store paths that *aren't* references.
The specific reasons for this will be given then, but we can still ask how how could this possibly be safe?
The fundamental answer is that since the drv file format is known to Nix, it can "do better" than plain scanning.
Nix knows how to parse them, and thus meaningfully differentiate between hashes based on *where* they occur.
It can decide which contexts correspond to an assumption that the store path ought to exist and be accessible, and which contexts do not.
