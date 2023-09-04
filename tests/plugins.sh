source common.sh

set -o pipefail

if [[ $BUILD_SHARED_LIBS != 1 ]]; then
    echo "plugins are not supported"
    exit 99
fi

res=$(nix --option setting-set true --option plugin-files $PWD/plugins/libplugintest* eval --expr builtins.anotherNull)

[ "$res"x = "nullx" ]
