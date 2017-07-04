export NIX_TEST_ROOT="$(cd -P -- "$(dirname -- "$0")" && pwd -P)"
source "$NIX_TEST_ROOT/common.sh"

setupTest

clearStore

path=$(nix-build $NIX_TEST_ROOT/dependencies.nix -o $TEST_ROOT/result)
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

# The path can be repaired by rebuilding the derivation.
nix-store --verify --check-contents --repair

nix-store --verify-path $path2

# Re-corrupt and delete the deriver. Now --verify --repair should
# not work.
chmod u+w $path2
touch $path2/bad

# FIXME: "error: cannot delete path $path2 since it is still alive"
nix-store --delete $(nix-store -qd $path2)

if nix-store --verify --check-contents --repair; then
    echo "nix-store --verify --repair succeeded unexpectedly" >&2
    exit 1
fi

nix-build $NIX_TEST_ROOT/dependencies.nix -o $TEST_ROOT/result --repair

if [ "$(nix-hash $path2)" != "$hash" -o -e $path2/bad ]; then
    echo "path not repaired properly" >&2
    exit 1
fi

cacheDir=$TEST_ROOT/binary-cache
nix copy --recursive --to file://$cacheDir $path

chmod u+w $path2
rm -rf $path2

nix-store --verify --check-contents --repair --option binary-caches "file://$cacheDir" --option signed-binary-caches ''

if [ "$(nix-hash $path2)" != "$hash" -o -e $path2/bad ]; then
    echo "path not repaired properly" >&2
    exit 1
fi

# Check --verify-path and --repair-path.
nix-store --verify-path $path2

chmod u+w $path2
rm -rf $path2

if nix-store --verify-path $path2; then
    echo "nix-store --verify-path succeeded unexpectedly" >&2
    exit 1
fi

nix-store --repair-path $path2 --option binary-caches "file://$cacheDir" --option signed-binary-caches ''

if [ "$(nix-hash $path2)" != "$hash" -o -e $path2/bad ]; then
    echo "path not repaired properly" >&2
    exit 1
fi
