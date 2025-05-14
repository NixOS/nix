---
synopsis: "Reduce connect timeout for http substituter"
issues:
prs: [12876]
---

Previously, the Nix setting `connect-timeout` had no limit. It is now set to `5s`, offering a more practical default for users self-hosting binary caches, which may occasionally become unavailable, such as during updates.
