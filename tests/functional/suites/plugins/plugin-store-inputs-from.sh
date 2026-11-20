# shellcheck shell=bash

# --inputs-from opens the eval store, whose type the plugin provides, only
# once the plugin is loaded.

plugin=$(findPlugin libplugintest)

dep=$TEST_ROOT/inputs-from-dep
mkdir -p "$dep"
echo '{ outputs = _: { x = "hi"; }; }' > "$dep/flake.nix"
top=$TEST_ROOT/inputs-from-top
mkdir -p "$top"
echo "{ inputs.dep.url = \"path:$dep\"; outputs = _: { }; }" > "$top/flake.nix"

# The store type itself works.
[[ "$(nix --option plugin-files "$plugin" eval --eval-store plugintest:// --expr 1)" = 1 ]]

[[ "$(nix --option plugin-files "$plugin" eval --eval-store plugintest:// --raw --inputs-from "$top" dep#x)" = hi ]]
