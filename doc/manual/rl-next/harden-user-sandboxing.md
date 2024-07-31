---
synopsis: Harden the user sandboxing
significance: significant
issues:
---

The build directory has been hardened against interference with the outside world by nesting it inside another directory owned by (and only readable by) the daemon user.

This is a low severity security fix, [CVE-2024-38531](https://www.cve.org/CVERecord?id=CVE-2024-38531), that was handled through the GitHub Security Advisories interface, and hence was merged directly in commit [2dd7f8f42](https://github.com/NixOS/nix/commit/2dd7f8f42da374d9fee4d424c1c6f82bcb36b393) instead of a PR.

Credit: [**@alois31**](https://github.com/alois31), [**Linus Heckemann (@lheckemann)**](https://github.com/lheckemann)
Co-authors: [**@edolstra**](https://github.com/edolstra)
