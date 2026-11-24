# shellcheck shell=bash

# The loaded plugin registers a primop that behaves differently depending on a
# setting value.

plugin=$(findPlugin libplugintest)

for options in "--option plugin-files" "--option extra-plugin-files" "--plugin-files" "--extra-plugin-files"; do
    # shellcheck disable=SC2086
    res=$(nix --option setting-set true $options "$plugin" eval --expr builtins.anotherNull)
    [ "$res"x = "nullx" ]
done
