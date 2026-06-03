---
synopsis: HTTP/3 (QUIC) support
---

Nix can now fetch from binary caches and other HTTP(S) sources over HTTP/3
(QUIC), controlled by a new
[`http3`](@docroot@/command-ref/conf-file.md#conf-http3) setting (disabled by
default). When enabled, Nix requests HTTP/3 and transparently falls back to
HTTP/2 or HTTP/1.1 for servers that do not advertise QUIC. The setting only
takes effect when linked against a libcurl built with HTTP/3 support, otherwise
it is ignored and Nix keeps using HTTP/2 without warning or error.

Enable it with:

```
nix.conf: http3 = true
CLI:      --http3
```

Or disable with:

```
nix.conf: http3 = false
CLI:      --no-http3
```
