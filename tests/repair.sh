source common.sh

clearStore

path=$(nix-build dependencies.nix -o $TEST_ROOT/result)
path2=$(nix-store -qR $path | grep input-2)

nix-store --verify --check-contents -v

hash=$(nix-hash $path2)

# Corrupt a path and check whether nix-build --repair can fix it.
chmod u+w $path2
touch $path2/bad

if nix-store --verify --check-contents -v; then
    echo "nix-store --verify succeeded unexpectedly" >&2
    exit 1
fi

if nix-store --verify --check-contents --repair; then
    echo "nix-store --verify --repair succeeded unexpectedly" >&2
    exit 1
fi

nix-build dependencies.nix -o $TEST_ROOT/result --repair

if [ "$(nix-hash $path2)" != "$hash" -o -e $path2/bad ]; then
    echo "path not repaired properly" >&2
    exit 1
fi

# Corrupt a path that has a substitute and check whether nix-store
# --verify can fix it.
clearCache

nix-push --dest $cacheDir $path

chmod u+w $path2
rm -rf $path2

nix-store --verify --check-contents --repair --option binary-caches "file://$cacheDir"

if [ "$(nix-hash $path2)" != "$hash" -o -e $path2/bad ]; then
    echo "path not repaired properly" >&2
    exit 1
fi
