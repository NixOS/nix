source common.sh

try () {
    printf "%s" "$2" > $TEST_ROOT/vector
    hash=$(nix hash-file --base16 $EXTRA --type "$1" $TEST_ROOT/vector)
    if test "$hash" != "$3"; then
        echo "hash $1, expected $3, got $hash"
        exit 1
    fi
}

try md5 "" "d41d8cd98f00b204e9800998ecf8427e"
try md5 "a" "0cc175b9c0f1b6a831c399e269772661"
try md5 "abc" "900150983cd24fb0d6963f7d28e17f72"
try md5 "message digest" "f96b697d7cb7938d525a2f31aaf161d0"
try md5 "abcdefghijklmnopqrstuvwxyz" "c3fcd3d76192e4007dfb496cca67e13b"
try md5 "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789" "d174ab98d277d9f5a5611c2c9f419d9f"
try md5 "12345678901234567890123456789012345678901234567890123456789012345678901234567890" "57edf4a22be3c955ac49da2e2107b67a"

try sha1 "" "da39a3ee5e6b4b0d3255bfef95601890afd80709"
try sha1 "abc" "a9993e364706816aba3e25717850c26c9cd0d89d"
try sha1 "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq" "84983e441c3bd26ebaae4aa1f95129e5e54670f1"

try sha256 "" "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"
try sha256 "abc" "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
try sha256 "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq" "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"

try sha512 "" "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e"
try sha512 "abc" "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"
try sha512 "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq" "204a8fc6dda82f0a0ced7beb8e08a41657c16ef468b228a8279be331a703c33596fd15c13b1b07f9aa1d3bea57789ca031ad85c7a71dd70354ec631238ca3445"

EXTRA=--base32
try sha256 "abc" "1b8m03r63zqhnjf7l5wnldhh7c134ap5vpj0850ymkq1iyzicy5s"
EXTRA=

EXTRA=--sri
try sha512 "" "sha512-z4PhNX7vuL3xVChQ1m2AB9Yg5AULVxXcg/SpIdNs6c5H0NE8XYXysP+DGNKHfuwvY7kxvUdBeoGlODJ6+SfaPg=="
try sha512 "abc" "sha512-3a81oZNherrMQXNJriBBMRLm+k6JqX6iCp7u5ktV05ohkpkqJ0/BqDa6PCOj/uu9RU1EI2Q86A4qmslPpUyknw=="
try sha512 "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq" "sha512-IEqPxt2oLwoM7XvrjgikFlfBbvRosiioJ5vjMacDwzWW/RXBOxsH+aodO+pXeJygMa2Fx6cd1wNU7GMSOMo0RQ=="
try sha256 "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq" "sha256-JI1qYdIGOLjlwCaTDD5gOaM85Flk/yFn9uzt1BnbBsE="

try2 () {
    hash=$(nix-hash --type "$1" $TEST_ROOT/hash-path)
    if test "$hash" != "$2"; then
        echo "hash $1, expected $2, got $hash"
        exit 1
    fi
}

rm -rf $TEST_ROOT/hash-path
mkdir $TEST_ROOT/hash-path
echo "Hello World" > $TEST_ROOT/hash-path/hello

try2 md5 "ea9b55537dd4c7e104515b2ccfaf4100"

# Execute bit matters.
chmod +x $TEST_ROOT/hash-path/hello
try2 md5 "20f3ffe011d4cfa7d72bfabef7882836"

# Mtime and other bits don't.
touch -r . $TEST_ROOT/hash-path/hello
chmod 744 $TEST_ROOT/hash-path/hello
try2 md5 "20f3ffe011d4cfa7d72bfabef7882836"

# File type (e.g., symlink) does.
rm $TEST_ROOT/hash-path/hello
ln -s x $TEST_ROOT/hash-path/hello
try2 md5 "f78b733a68f5edbdf9413899339eaa4a"

# Conversion.
try3() {
    h64=$(nix to-base64 --type "$1" "$2")
    [ "$h64" = "$4" ]
    sri=$(nix to-sri --type "$1" "$2")
    [ "$sri" = "$1-$4" ]
    h32=$(nix-hash --type "$1" --to-base32 "$2")
    [ "$h32" = "$3" ]
    h16=$(nix-hash --type "$1" --to-base16 "$h32")
    [ "$h16" = "$2" ]
    h16=$(nix to-base16 --type "$1" "$h64")
    [ "$h16" = "$2" ]
    h16=$(nix to-base16 "$sri")
    [ "$h16" = "$2" ]
}
try3 sha1 "800d59cfcd3c05e900cb4e214be48f6b886a08df" "vw46m23bizj4n8afrc0fj19wrp7mj3c0" "gA1Zz808BekAy04hS+SPa4hqCN8="
try3 sha256 "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" "1b8m03r63zqhnjf7l5wnldhh7c134ap5vpj0850ymkq1iyzicy5s" "ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0="
try3 sha512 "204a8fc6dda82f0a0ced7beb8e08a41657c16ef468b228a8279be331a703c33596fd15c13b1b07f9aa1d3bea57789ca031ad85c7a71dd70354ec631238ca3445" "12k9jiq29iyqm03swfsgiw5mlqs173qazm3n7daz43infy12pyrcdf30fkk3qwv4yl2ick8yipc2mqnlh48xsvvxl60lbx8vp38yji0" "IEqPxt2oLwoM7XvrjgikFlfBbvRosiioJ5vjMacDwzWW/RXBOxsH+aodO+pXeJygMa2Fx6cd1wNU7GMSOMo0RQ=="
