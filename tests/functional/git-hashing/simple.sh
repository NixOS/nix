source common.sh

repo="$TEST_ROOT/scratch"
git init "$repo"

git -C "$repo" config user.email "you@example.com"
git -C "$repo" config user.name "Your Name"

# Compare Nix's and git's implementation of git hashing
try () {
    local hash=$(nix hash path --mode git --format base16 --algo sha1 $TEST_ROOT/hash-path)
    [[ "$hash" == "$1" ]]

    git -C "$repo" rm -rf hash-path || true
    cp -r "$TEST_ROOT/hash-path" "$TEST_ROOT/scratch/hash-path"
    git -C "$repo" add hash-path
    git -C "$repo" commit -m "x"
    git -C "$repo" status
    local hash2=$(git -C "$TEST_ROOT/scratch" rev-parse HEAD:hash-path)
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

# Check Nix added object has matching git hash
try2 () {
    local hashPath="$1"
    local expected="$2"

    local path=$(nix store add --mode git --hash-algo sha1 "$repo/$hashPath")

    git -C "$repo" add "$hashPath"
    git -C "$repo" commit -m "x"
    git -C "$repo" status
    local hashFromGit=$(git -C "$repo" rev-parse "HEAD:$hashPath")
    [[ "$hashFromGit" == "$2" ]]

    local caFromNix=$(nix path-info --json "$path" | jq -r ".[] | .ca")
    [[ "fixed:git:sha1:$(nix hash convert --to nix32 "sha1:$hashFromGit")" = "$caFromNix" ]]
}

rm -rf "$repo/dummy1"
echo Hello World! > "$repo/dummy1"
try2 dummy1 "980a0d5f19a64b4b30a87d4206aade58726b60e3"

rm -rf "$repo/dummy2"
mkdir -p "$repo/dummy2"
echo Hello World! > "$repo/dummy2/hello"
try2 dummy2 "8b8e43b937854f4083ea56777821abda2799e850"

rm -rf "$repo/dummy3"
mkdir -p "$repo/dummy3"
mkdir -p "$repo/dummy3/dir"
touch "$repo/dummy3/dir/file"
echo Hello World! > "$repo/dummy3/dir/file"
touch "$repo/dummy3/dir/executable"
chmod +x "$repo/dummy3/dir/executable"
echo Run Hello World! > "$repo/dummy3/dir/executable"
try2 dummy3 "f227adfaf60d2778aabbf93df6dd061272d2dc85"

rm -rf "$repo/dummy4"
mkdir -p "$repo/dummy4"
mkdir -p "$repo/dummy4/dir"
touch "$repo/dummy4/dir/file"
ln -s './hello/world.txt' "$repo/dummy4/dir/symlink"
try2 dummy4 "06f3e789820fc488d602358f03e3a1cbf993bf33"
