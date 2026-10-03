---
synopsis: "`nix-store --repair-path` and `nix store repair` work through the daemon"
prs: []
---

`nix-store --repair-path` and `nix store repair` used to fail with "operation
'repairPath' is not supported by store 'daemon'". They now work for clients
that the daemon trusts, by asking the daemon to re-substitute the path (or
rebuild its deriver) in repair mode, as `nix-store --realise --repair` already
could. Untrusted clients get the same error as for `nix-store --realise
--repair`.
