source common.sh

clearStore

outPath1=$(echo 'with import ./config.nix; mkDerivation { name = "foo1"; builder = builtins.toFile "builder" "mkdir $out; echo hello > $out/foo"; }' | nix-build - --no-out-link --auto-optimise-store)
outPath2=$(echo 'with import ./config.nix; mkDerivation { name = "foo2"; builder = builtins.toFile "builder" "mkdir $out; echo hello > $out/foo"; }' | nix-build - --no-out-link --auto-optimise-store)

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

outPath3=$(echo 'with import ./config.nix; mkDerivation { name = "foo3"; builder = builtins.toFile "builder" "mkdir $out; echo hello > $out/foo"; }' | nix-build - --no-out-link)

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
