---
synopsis: Harden the user sandboxing
significance: significant
issues:
---

The build directory has been hardened against interference with the outside world by nesting it inside another directory owned by (and only readable by) the daemon user.
