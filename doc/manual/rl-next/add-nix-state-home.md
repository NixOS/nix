---
synopsis: Use envvars NIX_CACHE_HOME, NIX_CONFIG_HOME, NIX_DATA_HOME, NIX_STATE_HOME if defined
prs: [11351]
---

Look for 4 new environment variables: NIX_CACHE_HOME, NIX_CONFIG_HOME, NIX_DATA_HOME, NIX_STATE_HOME.
If one of them is defined, it takes precedence over its respective XDG envvar.

This provides more fine-grained control over where Nix looks for files, and allows to have a stand-alone Nix
environment, which only uses files in a specific directory, and doesn't touch any global user configuration.

For example, when [`use-xdg-base-directories`] is enabled, the configuration directory is:

1. `$NIX_CONFIG_HOME`, if it is defined
2. Otherwise, `$XDG_CONFIG_HOME/nix`, if `XDG_CONFIG_HOME` is defined
3. Otherwise, `~/.config/nix`.

Likewise for the state and cache directories.
