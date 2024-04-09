---
synopsis: Harden the user sandboxing
significance: significant
issues:
prs: <only provided once merged>
---

The build directory has been hardened against interference with the outside world by nesting it inside another directory owned by (and only readable by) the daemon user.
