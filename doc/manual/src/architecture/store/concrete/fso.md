# File System Object

Before we discuss the concrete Nix store's notion of a store object, we need to introduce the *file system object* data type it is built from.

Every file system object is one of the following:

 - File: an executable flag, and arbitrary data for contents
 - Directory: mapping of names to child file system objects
 - [Symbolic link][symlink]: may point anywhere.

```idris
data FileSystemObject
  = File {
      isExecutable : Bool,
      contents     : Bytes,
    }
  | Directory { entries : Map FileName FileSystemObject }
  | SymLink { target : Path }
```

Examples:

- a directory with contents

      /nix/store/<hash>-hello-2.10
      ├── bin
      │   └── hello
      └── share
          ├── info
          │   └── hello.info
          └── man
              └── man1
                  └── hello.1.gz

- a directory with relative symlink and other contents

      /nix/store/<hash>-go-1.16.9
      ├── bin -> share/go/bin
      ├── nix-support/
      └── share/

- a directory with absolute symlink

      /nix/store/d3k...-nodejs
      └── nix_node -> /nix/store/f20...-nodejs-10.24.

A bare file or symlink can be a root file system object.
Examples:

    /nix/store/<hash>-hello-2.10.tar.gz

    /nix/store/4j5...-pkg-config-wrapper-0.29.2-doc -> /nix/store/i99...-pkg-config-0.29.2-doc

Symlinks pointing outside of their own root or to a store object without a matching reference are allowed, but might not function as intended.
Examples:

- an arbitrarily symlinked file may change or not exist at all

      /nix/store/<hash>-foo
      └── foo -> /home/foo

- if a symlink to a store path was not automatically created by Nix, it may be invalid or get invalidated when the store object is deleted

      /nix/store/<hash>-bar
      └── bar -> /nix/store/abc...-foo

Nix file system objects do not support [hard links][hardlink]:
each file system object which is not the root has exactly one parent and one name.
However, as store objects are immutable, an underlying file system can use hard links for optimization.

[symlink]: https://en.m.wikipedia.org/wiki/Symbolic_link
[hardlink]: https://en.m.wikipedia.org/wiki/Hard_link
