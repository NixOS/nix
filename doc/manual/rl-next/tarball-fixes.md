---
synopsis: "Improve handling of tarballs that don't consist of a single top-level directory"
prs:
- 11195
---

In previous Nix releases, the tarball fetcher (used by `builtins.fetchTarball`) erroneously merged top-level directories into a single directory, and silently discarded top-level files that are not directories. This is no longer the case. The new behaviour is that *only* if the tarball consists of a single directory, the top-level path component of the files in the tarball is removed (similar to `tar`'s `--strip-components=1`).

Author: [**Eelco Dolstra (@edolstra)**](https://github.com/edolstra)
