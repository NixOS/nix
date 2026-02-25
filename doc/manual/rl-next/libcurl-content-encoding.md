---
synopsis: Content-Encoding decompression is now handled by libcurl
prs: [15336]
issues: [14324]
---

Transparent decompression of HTTP downloads specifying `Content-Encoding` header now uses libcurl. This adds support for previously advertised, but not supported `deflate` encoding as well as deprecated `x-gzip` alias.
Non-standard `xz`, `bzip2` encodings that were previously advertised are no longer supported, as they do not commonly appear in the wild and should not be sent by compliant servers.

`br`, `zstd`, `gzip` continue to be supported. Distro packaging should ensure that the `libcurl` dependency is linked against required libraries to support these encodings. By default now the build system requires libcurl >= 8.17.0 which is not known to have issues around [pausing and decompression](https://github.com/curl/curl/issues/16280).
