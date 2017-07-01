export NIX_TEST_ROOT="$(cd -P -- "$(dirname -- "$0")" && pwd -P)"
source "$NIX_TEST_ROOT/common.sh"

setupTest

clearStore

read -r -d '' scr1 <<EOF || true
with import $NIX_TEST_ROOT/config.nix;
mkDerivation {
    name = "foo1";
    builder = builtins.toFile "builder" "mkdir \$out; echo hello > \$out/foo";
}
EOF
outPath1=$(echo "$scr1" | nix-build - --no-out-link --option auto-optimise-store true)

read -r -d '' scr2 <<EOF || true
with import $NIX_TEST_ROOT/config.nix;
mkDerivation {
    name = "foo2";
    builder = builtins.toFile "builder" "mkdir \$out; echo hello >  \$out/foo";
}
EOF
outPath2=$(echo "$scr2" | nix-build - --no-out-link --option auto-optimise-store true)

inode1="$(stat --format=%i $outPath1/foo)"
inode2="$(stat --format=%i $outPath2/foo)"
if [ "$inode1" != "$inode2" ]; then
    echo "inodes do not match"
    exit 1
fi

nlink="$(stat --format=%h $outPath1/foo)"
if [ "$nlink" != 3 ]; then
    echo "link count incorrect"
    exit 1
fi

read -r -d '' scr3 <<EOF || true
with import $NIX_TEST_ROOT/config.nix;
mkDerivation {
    name = "foo3"; builder = builtins.toFile "builder" "mkdir \$out;
    echo hello > \$out/foo";
}
EOF
outPath3=$(echo "$scr3" | nix-build - --no-out-link)

inode3="$(stat --format=%i $outPath3/foo)"
if [ "$inode1" = "$inode3" ]; then
    echo "inodes match unexpectedly"
    exit 1
fi

nix-store --optimise

inode1="$(stat --format=%i $outPath1/foo)"
inode3="$(stat --format=%i $outPath3/foo)"
if [ "$inode1" != "$inode3" ]; then
    echo "inodes do not match"
    exit 1
fi

nix-store --gc

if [ -n "$(ls $NIX_STORE_DIR/.links)" ]; then
    echo ".links directory not empty after GC"
    exit 1
fi
