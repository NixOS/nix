# File System Object

Nix uses a simplified model of the file system, which consists of file system objects.
Every file system object is one of the following:

 - File

   - A possibly empty sequence of bytes for contents
   - A single boolean representing the [executable](https://en.m.wikipedia.org/wiki/File-system_permissions#Permissions) permission

 - Directory

   Mapping of names to child file system objects

 - [Symbolic link](https://en.m.wikipedia.org/wiki/Symbolic_link)

   An arbitrary string.
   Nix does not assign any semantics to symbolic links.

File systems objects and their children form a tree.
A bare file or symlink can be a root file system object.

## Examples of file system objects

A plain file:

```
. "Hello World", executable: false
```

An executable file:
```
. "#!/bin/bash\necho Hello World", executable: true
```

A directory with contents:

```
.
├── bin
│   └── hello, executable: true
└── share
    ├── info
    │   └── hello.info, executable: false
    └── man
        └── man1
            └── hello.1.gz, executable: false
```

A directory with relative symlink and other contents:

```
.
├── bin -> share/go/bin
├── nix-support/
└── share/
```

A directory with absolute symlink:

```
.
└── nix_node -> /nix/store/f20...-nodejs-10.24.
```
