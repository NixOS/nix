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

# The path can be repaired by rebuilding the derivation.
nix-store --verify --check-contents --repair

nix-store --verify-path $path2

# Re-corrupt and delete the deriver. Now --verify --repair should
# not work.
chmod u+w $path2
touch $path2/bad

nix-store --delete $(nix-store -qd $path2)

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

nix copy --to file://$cacheDir $path

chmod u+w $path2
rm -rf $path2

nix-store --verify --check-contents --repair --substituters "file://$cacheDir" --no-require-sigs

if [ "$(nix-hash $path2)" != "$hash" -o -e $path2/bad ]; then
    echo "path not repaired properly" >&2
    exit 1
fi

# Corrupt the mtime of a path and check that nix-store --verify --check-contents repairs it.
touch $path2/bar
nix-store --verify --check-contents
if [ "$(stat -c '%Y' $path2/bar)" != 1 ]; then
    echo "mtime not repaired properly" >&2
    exit 1
fi

# Give write permission to a path and check that nix-store --verify --check-contents removes it.
chmod -R +w $path2
nix-store --verify --check-contents
if [ "$(stat -c '%a' $path2)" != 555 ] || [ "$(stat -c '%a' $path2/bar)" != 444 ]; then
    echo "write permission not removed properly" >&2
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

nix-store --repair-path $path2 --substituters "file://$cacheDir" --no-require-sigs

if [ "$(nix-hash $path2)" != "$hash" -o -e $path2/bad ]; then
    echo "path not repaired properly" >&2
    exit 1
fi
