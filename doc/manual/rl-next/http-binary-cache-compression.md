---
synopsis: "HTTP binary caches now support transparent compression for metadata"
prs: []
---

HTTP binary cache stores can now compress `.narinfo`, `.ls`, and build log files before uploading them,
reducing bandwidth usage and storage requirements. The compression is applied transparently using the
`Content-Encoding` header, allowing compatible clients to automatically decompress the files.

Three new configuration options control this behavior:
- `narinfo-compression`: Compression method for `.narinfo` files
- `ls-compression`: Compression method for `.ls` files
- `log-compression`: Compression method for build logs in `log/` directory

Example usage:
```
nix copy --to 'http://cache.example.com?narinfo-compression=gzip&ls-compression=gzip' /nix/store/...
nix store copy-log --to 'http://cache.example.com?log-compression=br' /nix/store/...
```
