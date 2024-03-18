source common.sh

repo="$TEST_ROOT/scratch"
git init "$repo"

git -C "$repo" config user.email "you@example.com"
git -C "$repo" config user.name "Your Name"

try () {
    hash=$(nix hash path --mode git --format base16 --algo sha1 $TEST_ROOT/hash-path)
    [[ "$hash" == "$1" ]]

    git -C "$repo" rm -rf hash-path || true
    cp -r "$TEST_ROOT/hash-path" "$TEST_ROOT/scratch/hash-path"
    git -C "$repo" add hash-path
    git -C "$repo" commit -m "x"
    git -C "$repo" status
    hash2=$(git -C "$TEST_ROOT/scratch" rev-parse HEAD:hash-path)
    [[ "$hash2" = "$1" ]]
}

# blob
rm -rf $TEST_ROOT/hash-path
echo "Hello World" > $TEST_ROOT/hash-path
try "557db03de997c86a4a028e1ebd3a1ceb225be238"

# tree with children
rm -rf $TEST_ROOT/hash-path
mkdir $TEST_ROOT/hash-path
echo "Hello World" > $TEST_ROOT/hash-path/hello
echo "Run Hello World" > $TEST_ROOT/hash-path/executable
chmod +x $TEST_ROOT/hash-path/executable
try "e5c0a11a556801a5c9dcf330ca9d7e2c572697f4"

rm -rf $TEST_ROOT/dummy1
echo Hello World! > $TEST_ROOT/dummy1
path1=$(nix store add --mode git --hash-algo sha1 $TEST_ROOT/dummy1)
hash1=$(nix-store -q --hash $path1)
test "$hash1" = "sha256:1brffhvj2c0z6x8qismd43m0iy8dsgfmy10bgg9w11szway2wp9v"

rm -rf $TEST_ROOT/dummy2
mkdir -p $TEST_ROOT/dummy2
echo Hello World! > $TEST_ROOT/dummy2/hello
path2=$(nix store add --mode git --hash-algo sha1 $TEST_ROOT/dummy2)
hash2=$(nix-store -q --hash $path2)
test "$hash2" = "sha256:1vhv7zxam7x277q0y0jcypm7hwhccbzss81vkdgf0ww5sm2am4y0"

rm -rf $TEST_ROOT/dummy3
mkdir -p $TEST_ROOT/dummy3
mkdir -p $TEST_ROOT/dummy3/dir
touch $TEST_ROOT/dummy3/dir/file
echo Hello World! > $TEST_ROOT/dummy3/dir/file
touch $TEST_ROOT/dummy3/dir/executable
chmod +x $TEST_ROOT/dummy3/dir/executable
echo Run Hello World! > $TEST_ROOT/dummy3/dir/executable
path3=$(nix store add --mode git --hash-algo sha1 $TEST_ROOT/dummy3)
hash3=$(nix-store -q --hash $path3)
test "$hash3" = "sha256:08y3nm3mvn9qvskqnf13lfgax5lh73krxz4fcjd5cp202ggpw9nv"

rm -rf $TEST_ROOT/dummy3
mkdir -p $TEST_ROOT/dummy3
mkdir -p $TEST_ROOT/dummy3/dir
touch $TEST_ROOT/dummy3/dir/file
ln -s './hello/world.txt' $TEST_ROOT/dummy3/dir/symlink
path3=$(nix store add --mode git --hash-algo sha1 $TEST_ROOT/dummy3)
hash3=$(nix-store -q --hash $path3)
test "$hash3" = "sha256:1dwazas8irzpar89s8k2bnp72imfw7kgg4aflhhsfnicg8h428f3"
