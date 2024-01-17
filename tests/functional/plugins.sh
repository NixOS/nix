source common.sh

if [[ $BUILD_SHARED_LIBS != 1 ]]; then
    skipTest "Plugins are not supported"
fi

res=$(nix --option setting-set true --option plugin-files $PWD/plugins/libplugintest* eval --expr builtins.anotherNull)

[ "$res"x = "nullx" ]
