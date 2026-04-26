---
synopsis: "Linux sandbox: also block `listxattr` syscalls"
prs: [15743]
---

The Linux sandbox now also returns `ENOTSUP` for `listxattr`,
`llistxattr` and `flistxattr`, matching the existing treatment of
`getxattr`/`setxattr`/`removexattr`. This prevents host xattrs (e.g.
`security.selinux`) from leaking into builds and fixes tools such as
`mkfs.ubifs` that probe xattr support via `listxattr`.
