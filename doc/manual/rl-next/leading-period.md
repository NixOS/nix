---
synopsis: Store paths are allowed to start with `.`
issues: 912
prs: 9867 9091 9095 9120 9121 9122 9130 9219 9224
---

Leading periods were allowed by accident in Nix 2.4. The Nix team has considered this to be a bug, but this behavior has since been relied on by users, leading to unnecessary difficulties.
From now on, leading periods are officially, definitively supported. The names `.` and `..` are disallowed, as well as those starting with `.-` or `..-`.

Nix versions that denied leading periods are documented [in the issue](https://github.com/NixOS/nix/issues/912#issuecomment-1919583286).
