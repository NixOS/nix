source common.sh

set -o pipefail

res=$(nix eval --expr builtins.anotherNull --option setting-set true --option plugin-files $PWD/plugins/libplugintest*)

[ "$res"x = "nullx" ]
