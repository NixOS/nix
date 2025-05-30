---
synopsis: Support substituters using mTLS (client certificate) authentication 
issues: []
prs: [13030]
---

Added support for `ssl-cert` and `ssl-key` options in substituter URLs.

Example:

    https://substituter.invalid?ssl-cert=/path/to/cert.pem&ssl-key=/path/to/key.pem

When these options are configured, Nix will use this certificate/private key pair to authenticate to the server.
