# Binary Cache

The binary cache format is an interface designed for exposing a store over HTTP.

A binary cache consists of:

- A [`nix-cache-info`](./nix-cache-info.md) file at the root with remote-side configuration.
- For each [store object](@docroot@/store/store-object.md):
  - A [`.narinfo`](./narinfo.md) file containing the object's [metadata](@docroot@/store/store-object.md#metadata) and a (usually relative) URL to the corresponding compressed NAR.
  - A possibly-compressed [Nix Archive](@docroot@/store/file-system-object/content-address.md#serial-nix-archive) containing the store object's file system data.
- For every entry in the [build trace](@docroot@/store/build-trace.md), a JSON file at `build-trace-v2/<drvBaseName>/<outputName>.doi`:
  - the path encodes the [key](@docroot@/protocols/json/build-trace-entry.md#key)
  - the contents are the [value](@docroot@/protocols/json/build-trace-entry.md#value).

The following [store types](@docroot@/store/types/index.md) use the binary cache format:

- [HTTP Binary Cache Store](@docroot@/store/types/http-binary-cache-store.md) — served over HTTP(S)
- [Local Binary Cache Store](@docroot@/store/types/local-binary-cache-store.md) — stored on the file system
- [S3 Binary Cache Store](@docroot@/store/types/s3-binary-cache-store.md) — stored in an AWS S3 bucket
