# shellcheck shell=bash

source common.sh

# Assert is set
[[ ${hashAlgo+x} ]]

repo="$TEST_ROOT/scratch"

initRepo () {
    git init "$repo" --object-format="$hashAlgo"

    git -C "$repo" config user.email "you@example.com"
    git -C "$repo" config user.name "Your Name"
}

# Compare Nix's and git's implementation of git hashing
try () {
    local expected="$1"

    local hash
    hash=$(nix hash path --mode git --format base16 --algo "$hashAlgo" "$TEST_ROOT/hash-path")
    [[ "$hash" == "$expected" ]]

    git -C "$repo" rm -rf hash-path || true
    cp -r "$TEST_ROOT/hash-path" "$repo/hash-path"
    git -C "$repo" add hash-path
    git -C "$repo" commit -m "x"
    git -C "$repo" status
    local hash2
    hash2=$(git -C "$repo" rev-parse HEAD:hash-path)
    [[ "$hash2" = "$expected" ]]
}

# Check Nix added object has matching git hash
try2 () {
    local hashPath="$1"
    local expected="$2"

    local path
    path=$(nix store add --mode git --hash-algo "$hashAlgo" "$repo/$hashPath")

    git -C "$repo" add "$hashPath"
    git -C "$repo" commit -m "x"
    git -C "$repo" status
    local hashFromGit
    hashFromGit=$(git -C "$repo" rev-parse "HEAD:$hashPath")
    [[ "$hashFromGit" == "$expected" ]]

    nix path-info --json "$path" | jq -e \
        --arg algo "$hashAlgo" \
        --arg hash "$(nix hash convert --to base64 "$hashAlgo:$hashFromGit")" \
        '.[].ca == {
            method: "git",
            hash: {
                algorithm: $algo,
                format: "base64",
                hash: $hash
            },
        }'
}

test0 () {
    rm -rf "$TEST_ROOT/hash-path"
    echo "Hello World" > "$TEST_ROOT/hash-path"
}

test1 () {
    rm -rf "$TEST_ROOT/hash-path"
    mkdir "$TEST_ROOT/hash-path"
    echo "Hello World" > "$TEST_ROOT/hash-path/hello"
    echo "Run Hello World" > "$TEST_ROOT/hash-path/executable"
    chmod +x "$TEST_ROOT/hash-path/executable"
}

test2 () {
    rm -rf "$repo/dummy1"
    echo Hello World! > "$repo/dummy1"
}

test3 () {
    rm -rf "$repo/dummy2"
    mkdir -p "$repo/dummy2"
    echo Hello World! > "$repo/dummy2/hello"
}

test4 () {
    rm -rf "$repo/dummy3"
    mkdir -p "$repo/dummy3"
    mkdir -p "$repo/dummy3/dir"
    touch "$repo/dummy3/dir/file"
    echo Hello World! > "$repo/dummy3/dir/file"
    touch "$repo/dummy3/dir/executable"
    chmod +x "$repo/dummy3/dir/executable"
    echo Run Hello World! > "$repo/dummy3/dir/executable"
}

test5 () {
    rm -rf "$repo/dummy4"
    mkdir -p "$repo/dummy4"
    mkdir -p "$repo/dummy4/dir"
    touch "$repo/dummy4/dir/file"
    ln -s './hello/world.txt' "$repo/dummy4/dir/symlink"
}
