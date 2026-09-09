# shellcheck shell=bash

# The loaded plugin registers a primop that behaves differently depending on a
# setting value.

plugin=$(findPlugin libplugintest)
res=$(nix --option setting-set true --option plugin-files "$plugin" eval --expr builtins.anotherNull)
[ "$res"x = "nullx" ]
