# Import From Derivation

The value of a Nix expression can depend on the contents of a [store object].
In this case, when that store object is needed, evaluation will be paused, the store object [realised], and then evaluation resumed.

[store object]: @docroot@/glossary.md#gloss-store-object
[derivation]: @docroot@/glossary.md#gloss-derivation
[realised]: @docroot@/glossary.md#gloss-realise

This has performance implications:
Since evaluation is sequential, each required store object that is not already in the store will also be realised sequentially.

Passing a store path to any built-in function that reads from the filesystem constitutes Import From Derivation:

- [`import`](./builtins.md#builtins-import)` expr`
- [`builtins.readFile`](./builtins.md#builtins-readFile)` expr`
- [`builtins.readFileType`](./builtins.md#builtins-readFileType)` expr`
- [`builtins.readDir`](./builtins.md#builtins-readDir)` expr`
- [`builtins.pathExists`](./builtins.md#builtins-pathExists)` expr`
- [`builtins.filterSource`](./builtins.md#builtins-filterSource)` f expr`
- [`builtins.path`](./builtins.md#builtins-path)` { path = expr; }`
- [`builtins.hashFile`](./builtins.md#builtins-hashFile)` t expr`
- `builtins.scopedImport x drv`

Realising store objects during evaluation can be disabled by setting [`allow-import-from-derivation`](../command-ref/conf-file.md#conf-allow-import-from-derivation) to `false`.

## Example

In the following Nix expression, the inner derivation `drv` produces a file containing `"hello"`.

```nix
# IFD.nix
let
  drv = derivation {
    name = "hello";
    builder = /bin/sh;
    args = [ "-c" ''echo \"hello\" > $out'' ];
    system = builtins.currentSystem;
  };
in "${import drv} world"
```

```shellSession
nix-instantiate IFD.nix --eval --read-write-mode
```

```
building '/nix/store/348q1cal6sdgfxs8zqi9v8llrsn4kqkq-hello.drv'...
"hello world"
```

Since `"hello"` is a valid Nix expression, it can be [`import`](./builtins.md#builtins-import)ed.
That requires reading from the output [store path](@docroot@/glossary.md#gloss-store-path) of `drv`, which has to be [realised] before its contents can be read and evaluated.

## Illustration

The following diagram shows how evaluation is interrupted by a build, if the value of a Nix expression depends on realising a store object.

```
+----------------------+             +------------------------+
| Nix language         |             | Nix store              |
|  .----------------.  |             |                        |
|  | Nix expression |  |             |                        |
|  '----------------'  |             |                        |
|          |           |             |                        |
|       evaluate       |             |                        |
|          |           |             |                        |
|          V           |             |                        |
|    .------------.    |             |  .------------------.  |
|    | derivation |----|-instantiate-|->| store derivation |  |
|    '------------'    |             |  '------------------'  |
|                      |             |           |            |
|                      |             |        realise         |
|                      |             |           |            |
|                      |             |           V            |
|  .----------------.  |             |    .--------------.    |
|  | Nix expression |<-|----read-----|----| store object |    |
|  '----------------'  |             |    '--------------'    |
|          |           |             |                        |
|       evaluate       |             |                        |
|          |           |             |                        |
|          V           |             |                        |
|    .------------.    |             |                        |
|    |   value    |    |             |                        |
|    '------------'    |             |                        |
+----------------------+             +------------------------+
```
