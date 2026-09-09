# shellcheck shell=bash

# Test whether -c library API symbols are exported and can be looked up via
# dlsym.

nix --option plugin-files "$(findPlugin libplugintest_dlsym)" eval --expr 0 >/dev/null
