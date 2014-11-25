source common.sh

clearStore

outPath1=$(echo 'with import ./config.nix; mkDerivation { name = "foo1"; builder = builtins.toFile "builder" "mkdir $out; echo hello > $out/foo"; }' | nix-build - --no-out-link --option auto-optimise-store true)
outPath2=$(echo 'with import ./config.nix; mkDerivation { name = "foo2"; builder = builtins.toFile "builder" "mkdir $out; echo hello > $out/foo"; }' | nix-build - --no-out-link --option auto-optimise-store true)

inode1="$(perl -e "print ((lstat('$outPath1/foo'))[1])")"
inode2="$(perl -e "print ((lstat('$outPath2/foo'))[1])")"
if [ "$inode1" != "$inode2" ]; then
    echo "inodes do not match"
    exit 1
fi

nlink="$(perl -e "print ((lstat('$outPath1/foo'))[3])")"
if [ "$nlink" != 3 ]; then
    echo "link count incorrect"
    exit 1
fi

outPath3=$(echo 'with import ./config.nix; mkDerivation { name = "foo3"; builder = builtins.toFile "builder" "mkdir $out; echo hello > $out/foo"; }' | nix-build - --no-out-link)

inode3="$(perl -e "print ((lstat('$outPath3/foo'))[1])")"
if [ "$inode1" = "$inode3" ]; then
    echo "inodes match unexpectedly"
    exit 1
fi

nix-store --optimise

inode1="$(perl -e "print ((lstat('$outPath1/foo'))[1])")"
inode3="$(perl -e "print ((lstat('$outPath3/foo'))[1])")"
if [ "$inode1" != "$inode3" ]; then
    echo "inodes do not match"
    exit 1
fi

nix-store --gc

if [ -n "$(ls $NIX_STORE_DIR/.links)" ]; then
    echo ".links directory not empty after GC"
    exit 1
fi
