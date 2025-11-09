---
synopsis: Channel URLs migrated to channels.nixos.org subdomain
prs: [14518]
issues: [14517]
---

Channel URLs have been updated from `https://nixos.org/channels/` to `https://channels.nixos.org/` throughout Nix.

The subdomain provides better reliability with IPv6 support and improved CDN distribution. The old domain apex (`nixos.org/channels/`) currently redirects to the new location but may be deprecated in the future.
