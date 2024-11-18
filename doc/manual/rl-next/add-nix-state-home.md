---
synopsis: Use envvars NIX_CACHE_HOME, NIX_CONFIG_HOME, NIX_DATA_HOME, NIX_STATE_HOME if defined
prs: [11351]
---

Added new environment variables:

- `NIX_CACHE_HOME`
- `NIX_CONFIG_HOME`
- `NIX_DATA_HOME`
- `NIX_STATE_HOME`

Each, if defined, takes precedence over the corresponding [XDG environment variable](@docroot@/command-ref/env-common.md#xdg-base-directories).
This provides more fine-grained control over where Nix looks for files, and allows to have a stand-alone Nix environment, which only uses files in a specific directory, and doesn't interfere with the user environment.
