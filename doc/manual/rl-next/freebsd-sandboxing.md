---
synopsis: Enable FreeBSD sandboxing, add `x86_64-freebsd` to installer
prs: [15673, 13281, 9968]
---

A FreeBSD build has been added to the traditional installer script, with sandboxing enabled.
The beta installer is not yet supported.

FreeBSD support is not as well-tested as Linux or macOS, but is fully capable of building packages
and performing other tasks expected of Nix on Linux.
