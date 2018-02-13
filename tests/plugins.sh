source common.sh

set -o pipefail

res=$(nix eval '(builtins.anotherNull)' --option plugin-files $PWD/plugins/libplugintest*)

[ "$res"x = "nullx" ]
