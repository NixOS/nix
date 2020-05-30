source common.sh

try () {
    hash=$(nix hash-git --base16 --type sha1 $TEST_ROOT/hash-path)
    if test "$hash" != "$1"; then
        echo "git hash, expected $1, got $hash"
        exit 1
    fi
}

rm -rf $TEST_ROOT/hash-path
mkdir $TEST_ROOT/hash-path
echo "Hello World" > $TEST_ROOT/hash-path/hello

try "117c62a8c5e01758bd284126a6af69deab9dbbe2"

rm -rf $TEST_ROOT/dummy1
echo Hello World! > $TEST_ROOT/dummy1
path1=$(nix add-to-store --git $TEST_ROOT/dummy1)
hash1=$(nix-store -q --hash $path1)
test "$hash1" = "sha1:$(nix hash-git --type sha1 --base32 $TEST_ROOT/dummy1)"

rm -rf $TEST_ROOT/dummy2
mkdir -p $TEST_ROOT/dummy2
echo hello > $TEST_ROOT/dummy2/hello
path2=$(nix add-to-store --git $TEST_ROOT/dummy2)
hash2=$(nix-store -q --hash $path2)
test "$hash2" = "sha1:$(nix hash-git --type sha1 --base32 $TEST_ROOT/dummy2)"

rm -rf $TEST_ROOT/dummy3
mkdir -p $TEST_ROOT/dummy3
mkdir -p $TEST_ROOT/dummy3/hello
echo hello > $TEST_ROOT/dummy3/hello/hello
path3=$(nix add-to-store --git $TEST_ROOT/dummy3)
hash3=$(nix-store -q --hash $path3)
test "$hash3" = "sha1:$(nix hash-git --type sha1 --base32 $TEST_ROOT/dummy3)"
