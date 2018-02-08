source common.sh

set -o pipefail

res=$(nix eval '(builtins.constNull true)' --option plugin-files $PWD/plugins/plugintest.so)

[ "$res"x = "nullx" ]
