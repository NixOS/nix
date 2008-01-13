source common.sh

try () {
    printf "%s" "$2" > $TEST_ROOT/vector
    hash=$($nixhash $EXTRA --flat --type "$1" $TEST_ROOT/vector)
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

try sha1 "abc" "a9993e364706816aba3e25717850c26c9cd0d89d"
try sha1 "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq" "84983e441c3bd26ebaae4aa1f95129e5e54670f1"

try sha256 "abc" "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
try sha256 "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq" "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"

EXTRA=--base32
try sha256 "abc" "1b8m03r63zqhnjf7l5wnldhh7c134ap5vpj0850ymkq1iyzicy5s"
EXTRA=

try2 () {
    hash=$($nixhash --type "$1" $TEST_ROOT/hash-path)
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
touch -r $TOP $TEST_ROOT/hash-path/hello
chmod 744 $TEST_ROOT/hash-path/hello
try2 md5 "20f3ffe011d4cfa7d72bfabef7882836"

# File type (e.g., symlink) does.
rm $TEST_ROOT/hash-path/hello
ln -s x $TEST_ROOT/hash-path/hello
try2 md5 "f78b733a68f5edbdf9413899339eaa4a"

# Conversion.
test $($nixhash --type sha256 --to-base32 "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad") = "1b8m03r63zqhnjf7l5wnldhh7c134ap5vpj0850ymkq1iyzicy5s"
test $($nixhash --type sha256 --to-base16 "1b8m03r63zqhnjf7l5wnldhh7c134ap5vpj0850ymkq1iyzicy5s") = "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
test $($nixhash --type sha1 --to-base32 "800d59cfcd3c05e900cb4e214be48f6b886a08df") = "vw46m23bizj4n8afrc0fj19wrp7mj3c0"
test $($nixhash --type sha1 --to-base16 "vw46m23bizj4n8afrc0fj19wrp7mj3c0") = "800d59cfcd3c05e900cb4e214be48f6b886a08df"
