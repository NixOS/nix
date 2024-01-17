---
synopsis: Include cgroup stats when building through the daemon
prs: 9598
---

Nix now also reports cgroup statistics when building through the nix daemon and when doing remote builds using ssh-ng,
if both sides of the connection are this version of Nix or newer. 

