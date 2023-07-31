R"(

**Store URL format**: `file://`*path*

This store allows reading and writing a binary cache stored in *path*
in the local filesystem. If *path* does not exist, it will be created.

For example, the following builds or downloads `nixpkgs#hello` into
the local store and then copies it to the binary cache in
`/tmp/binary-cache`:

```
# nix copy --to file:///tmp/binary-cache nixpkgs#hello
```

)"
