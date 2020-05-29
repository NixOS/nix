source common.sh

path1=$(nix-store --add ./dummy)
echo $path1

path2=$(nix-store --add-fixed sha256 --recursive ./dummy)
echo $path2

if test "$path1" != "$path2"; then
    echo "nix-store --add and --add-fixed mismatch"
    exit 1
fi    

path3=$(nix-store --add-fixed sha256 ./dummy)
echo $path3
test "$path1" != "$path3" || exit 1

path4=$(nix-store --add-fixed sha1 --recursive ./dummy)
echo $path4
test "$path1" != "$path4" || exit 1

hash1=$(nix-store -q --hash $path1)
echo $hash1

hash2=$(nix-hash --type sha256 --base32 ./dummy)
echo $hash2

test "$hash1" = "sha256:$hash2"

# Git tests

path5=$(nix add-to-store --git ./dummy)
hash3=$(nix-store -q --hash $path5)
test "$hash3" = "sha1:$(nix hash-git --type sha1 --base32 ./dummy)"

rm -rf dummy2
mkdir -p dummy2
echo hello > dummy2/hello
path6=$(nix add-to-store --git ./dummy2)
hash4=$(nix-store -q --hash $path6)
test "$hash4" = "sha1:$(nix hash-git --type sha1 --base32 ./dummy2)"

rm -rf dummy3
mkdir -p dummy3
mkdir -p dummy3/hello
echo hello > dummy3/hello/hello
path7=$(nix add-to-store --git ./dummy3)
hash5=$(nix-store -q --hash $path7)
test "$hash5" = "sha1:$(nix hash-git --type sha1 --base32 ./dummy3)"
