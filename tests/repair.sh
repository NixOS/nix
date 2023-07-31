source common.sh

needLocalStore "--repair needs a local store"

clearStore

path=$(nix-build dependencies.nix -o $TEST_ROOT/result)
path2=$(nix-store -qR $path | grep input-2)

nix-store --verify --check-contents -v

hash=$(nix-hash $path2)

# Corrupt a path and check whether nix-build --repair can fix it.
chmod u+w $path2
touch $path2/bad

(! nix-store --verify --check-contents -v)

# The path can be repaired by rebuilding the derivation.
nix-store --verify --check-contents --repair

(! [ -e $path2/bad ])
(! [ -w $path2 ])

nix-store --verify-path $path2

# Re-corrupt and delete the deriver. Now --verify --repair should
# not work.
chmod u+w $path2
touch $path2/bad

nix-store --delete $(nix-store -q --referrers-closure $(nix-store -qd $path2))

(! nix-store --verify --check-contents --repair)

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

# Check that --repair-path also checks content of optimised symlinks (1/2)
nix-store --verify-path $path2

if (! nix-store --optimize); then
    echo "nix-store --optimize failed to optimize the store" >&2
    exit 1
fi
chmod u+w $path2/bar
echo 'rabrab' > $path2/bar # different length

if nix-store --verify-path $path2; then
    echo "nix-store --verify-path did not detect .links file corruption" >&2
    exit 1
fi

nix-store --repair-path $path2 --option auto-optimise-store true

if [ "$(nix-hash $path2)" != "$hash" -o "BAR" != "$(< $path2/bar)" ]; then
    echo "path not repaired properly" >&2
    exit 1
fi

# Check that --repair-path also checks content of optimised symlinks (2/2)
nix-store --verify-path $path2

if (! nix-store --optimize); then
    echo "nix-store --optimize failed to optimize the store" >&2
    exit 1
fi
chmod u+w $path2
chmod u+w $path2/bar
sed -e 's/./X/g' < $path2/bar > $path2/tmp # same length, different content.
cp $path2/tmp $path2/bar
rm $path2/tmp

if nix-store --verify-path $path2; then
    echo "nix-store --verify-path did not detect .links file corruption" >&2
    exit 1
fi

nix-store --repair-path $path2 --substituters "file://$cacheDir" --no-require-sigs --option auto-optimise-store true

if [ "$(nix-hash $path2)" != "$hash" -o "BAR" != "$(< $path2/bar)" ]; then
    echo "path not repaired properly" >&2
    exit 1
fi
