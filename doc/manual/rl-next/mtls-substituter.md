---
synopsis: Support HTTPS binary caches using mTLS (client certificate) authentication
issues: [13002]
prs: [13030]
---

Added support for `tls-certificate` and `tls-private-key` options in substituter URLs.

Example:

```
https://substituter.invalid?tls-certificate=/path/to/cert.pem&tls-private-key=/path/to/key.pem
```

When these options are configured, Nix will use this certificate/private key pair to authenticate to the server.
