source common.sh

set -o pipefail

res=$(nix --option setting-set true --option plugin-files $PWD/plugins/libplugintest* eval --expr builtins.anotherNull)

[ "$res"x = "nullx" ]
