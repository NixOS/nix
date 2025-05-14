---
synopsis: "Add host attribute of github/gitlab flakerefs to URL serialization"
issues:
prs: [12580]
---

Resolved an issue where `github:` or `gitlab:` URLs lost their `host` attribute when written to a lockfile, resulting in invalid URLs.
