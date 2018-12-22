source common.sh

set -o pipefail

res=$(nix --option plugin-files $PWD/plugins/libplugintest* eval '(builtins.anotherNull)' --option setting-set true)
[ "$res"x = "nullx" ]

res=$(nix --option plugin-files $PWD/plugins/libplugintest* sayhi)
[ "$res"x = "Hi!x" ]

nix --option plugin-files $PWD/plugins/libplugintest* 2>&1 | grep sayhi >/dev/null
