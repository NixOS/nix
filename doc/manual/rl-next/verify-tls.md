---
synopsis: "`<nix/fetchurl.nix>` uses TLS verification"
prs: [11585]
---

Previously `<nix/fetchurl.nix>` did not do TLS verification. This was because the Nix sandbox in the past did not have access to TLS certificates, and Nix checks the hash of the fetched file anyway. However, this can expose authentication data from `netrc` and URLs to man-in-the-middle attackers. In addition, Nix now in some cases (such as when using impure derivations) does *not* check the hash. Therefore we have now enabled TLS verification. This means that downloads by `<nix/fetchurl.nix>` will now fail if you're fetching from a HTTPS server that does not have a valid certificate.

`<nix/fetchurl.nix>` is also known as the builtin derivation builder `builtin:fetchurl`. It's not to be confused with the evaluation-time function `builtins.fetchurl`, which was not affected by this issue.
