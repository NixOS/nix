---
synopsis: "Fix Git LFS SSH issues"
prs: [13743]
issues: [13337]
---

Fixed some outstanding issues with Git LFS and SSH.

* Added support for `NIX_SSHOPTS`.
* Properly use the parsed port from URL.
* Better use of the response of `git-lfs-authenticate` to determine API endpoint when the API is not exposed on port 443.
