# The concrete model

The concrete model reconciles this abstract vision with the reality of Unix as it exists today.
Store objects own file system data, reference can be encoded as file system paths, and build steps run arbitrary processes.
As none of these three Unix abstractions are inherently "functional" per the properties above, it is up to Nix to enforce those properties.

## Files and Processes

Nix maps between its store model and the [Unix paradigm][unix-paradigm] of [files and processes][file-descriptor], by encoding immutable store objects and opaque identifiers as file system primitives: files and directories, and paths.
That allows processes to resolve references contained in files and thus access the contents of store objects.

Store objects are therefore implemented as the pair of

  - a *file system object* for data
  - a set of *store paths* for references.

[unix-paradigm]: https://en.m.wikipedia.org/wiki/Everything_is_a_file
[file-descriptor]: https://en.m.wikipedia.org/wiki/File_descriptor

```
+-----------------------------------------------------------------+
| Nix                                                             |
|                  [ commmand line interface ]------,             |
|                               |                   |             |
|                           evaluates               |             |
|                               |                manages          |
|                               V                   |             |
|                  [ configuration language  ]      |             |
|                               |                   |             |
| +-----------------------------|-------------------V-----------+ |
| | store                  evaluates to                         | |
| |                             |                               | |
| |             referenced by   V       builds                  | |
| |  [ build input ] ---> [ build plan ] ---> [ build result ]  | |
| |         ^                                        |          | |
| +---------|----------------------------------------|----------+ |
+-----------|----------------------------------------|------------+
            |                                        |
    file system object                          store path
            |                                        |
+-----------|----------------------------------------|------------+
| operating system        +------------+             |            |
|           '------------ |            | <-----------'            |
|                         |    file    |                          |
|                     ,-- |            | <-,                      |
|                     |   +------------+   |                      |
|          execute as |                    | read, write, execute |
|                     |   +------------+   |                      |
|                     '-> |  process   | --'                      |
|                         +------------+                          |
+-----------------------------------------------------------------+
```

## One interface, many implementations

There exists different types of stores, which all follow this model.
Examples:
- store on the local file system
- remote store accessible via SSH
- binary cache store accessible via HTTP

We see in the latter two that there is room for flexibility.
Builds can be distributed to multiple machines, and data must only be "exposed" via conventional OS interfaces during build steps, being free to be stored "at rest" in other less conventional ways.
