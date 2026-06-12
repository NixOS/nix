---
synopsis: "Nix daemon can serve store paths over sockets without filesystem access"
---

The Nix daemon can now serve store paths purely over Unix domain sockets without
requiring the client to have filesystem access to the store directory. This can be
useful for VM setups where the host serves store paths to the guest via socket,
with the guest having no direct access to the host's `/nix/store`.

This works for copying paths, substitution, and building.
