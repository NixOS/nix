---
synopsis: "Support unit prefixes in configuration settings"
prs: 10668
---

Configuration settings in Nix now support unit prefixes, allowing for more intuitive and readable configurations. For example, you can now specify [`--min-free 1G`](@docroot@/command-ref/opt-common.md#opt-min-free) to set the minimum free space to 1 gigabyte.

This enhancement was extracted from [#7851](https://github.com/NixOS/nix/pull/7851) and is also useful for PR [#10661](https://github.com/NixOS/nix/pull/10661).

Author: [**Eelco Dolstra (@edolstra)**](https://github.com/edolstra)
