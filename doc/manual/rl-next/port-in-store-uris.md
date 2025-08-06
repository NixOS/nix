---
synopsis: "Add support for user@address:port syntax in store URIs"
prs: [3425]
issues: [7044]
---

It's now possible to specify the port used for the SSH stores directly in the store URL in accordance with [RFC3986](https://datatracker.ietf.org/doc/html/rfc3986). Previously the only way to specify custom ports was via `ssh_config` or `NIX_SSHOPTS` environment variable, because Nix incorrectly passed the port number together with the host name to the SSH executable. This has now been fixed.

This change affects [store references](@docroot@/store/types/index.md#store-url-format) passed via the `--store` and similar flags in CLI as well as in the configuration for [remote builders](@docroot@/command-ref/conf-file.md#conf-builders). For example, the following store URIs now work:

- `ssh://127.0.0.1:2222`
- `ssh://[b573:6a48:e224:840b:6007:6275:f8f7:ebf3]:22`
- `ssh-ng://[b573:6a48:e224:840b:6007:6275:f8f7:ebf3]:22`
