source common.sh

path1=$($nixstore --add ./dummy)
echo $path1

path2=$($nixstore --add-fixed sha256 --recursive ./dummy)
echo $path2

if test "$path1" != "$path2"; then
    echo "nix-store --add and --add-fixed mismatch"
    exit 1
fi    

path3=$($nixstore --add-fixed sha256 ./dummy)
echo $path3
test "$path1" != "$path3" || exit 1

path4=$($nixstore --add-fixed sha1 --recursive ./dummy)
echo $path4
test "$path1" != "$path4" || exit 1

hash1=$($nixstore -q --hash $path1)
echo $hash1

hash2=$($nixhash --type sha256 --base32 ./dummy)
echo $hash2

test "$hash1" = "sha256:$hash2"
