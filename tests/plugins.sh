source common.sh

if [[ "$(uname)" =~ ^MINGW|^MSYS ]]; then
    exit 99
fi

set -o pipefail

res=$(nix eval '(builtins.anotherNull)' --option setting-set true --option plugin-files $PWD/plugins/libplugintest*)

[ "$res"x = "nullx" ]
