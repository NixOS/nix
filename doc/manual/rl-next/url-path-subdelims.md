---
synopsis: URL paths no longer percent-encode `+` and other sub-delims
prs: [15510]
---

URL path segments containing characters from the RFC 3986 sub-delims set
(`!$&'()*+,;=`) now render unencoded. Previously `file:///tmp/a+b` would
round-trip through `ParsedURL::to_string()` as `file:///tmp/a%2Bb`; it now
round-trips as itself. This fixes confusing `%2B` appearing in error messages,
`nix store info` output, and flake lock files when filesystem paths contain `+`.

Store URLs with `+` in their path will produce a different NarInfoDiskCache key
on upgrade, causing a one-time cache miss (the cache repopulates on the next
query).
