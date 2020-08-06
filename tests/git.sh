source common.sh

clearStore
clearCache

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
test "$hash1" = "sha256:1brffhvj2c0z6x8qismd43m0iy8dsgfmy10bgg9w11szway2wp9v"

rm -rf $TEST_ROOT/dummy2
mkdir -p $TEST_ROOT/dummy2
echo Hello World! > $TEST_ROOT/dummy2/hello
path2=$(nix add-to-store --git $TEST_ROOT/dummy2)
hash2=$(nix-store -q --hash $path2)
test "$hash2" = "sha256:1vhv7zxam7x277q0y0jcypm7hwhccbzss81vkdgf0ww5sm2am4y0"

rm -rf $TEST_ROOT/dummy3
mkdir -p $TEST_ROOT/dummy3
mkdir -p $TEST_ROOT/dummy3/hello
echo Hello World! > $TEST_ROOT/dummy3/hello/hello
path3=$(nix add-to-store --git $TEST_ROOT/dummy3)
hash3=$(nix-store -q --hash $path3)
test "$hash3" = "sha256:1i2x80840igikhbyy7nqf08ymx3a6n83x1fzyrxvddf0sdl5nqvp"

if [[ -n $(type -p git) ]]; then
    repo=$TEST_ROOT/git

    rm -rf $repo $TEST_HOME/.cache/nix

    git init $repo
    git -C $repo config user.email "foobar@example.com"
    git -C $repo config user.name "Foobar"

    echo utrecht > $repo/hello
    touch $repo/.gitignore
    git -C $repo add hello .gitignore
    git -C $repo commit -m 'Bla1'

    echo world > $repo/hello
    git -C $repo commit -m 'Bla2' -a

    treeHash=$(git -C $repo rev-parse HEAD:)

    # Fetch the default branch.
    path=$(nix eval --raw --expr "(builtins.fetchTree { type = \"git\"; url = file://$repo; treeHash = \"$treeHash\"; }).outPath")
    [[ $(cat $path/hello) = world ]]

    # Submodules cause error.
    (! nix eval --raw --expr "(builtins.fetchTree { type = \"git\"; url = file://$repo; treeHash = \"$treeHash\"; submodules = true; }).outPath")

    # Check that we can substitute it from other places.
    nix copy --to file://$cacheDir $path
    nix-store --delete $path
    path2=$(nix eval --raw --expr "(builtins.fetchTree { type = \"git\"; url = file:///no-such-repo; treeHash = \"$treeHash\"; }).outPath" --substituters file://$cacheDir --option substitute true)
    [ $path2 = $path ]

    # HEAD should be the same path as tree
    path3=$(nix eval --impure --raw --expr "(builtins.fetchTree { type = \"git\"; url = file://$repo; ref = \"HEAD\"; gitIngestion = true; }).outPath")
    [ $path3 = $path ]
else
    echo "Git not installed; skipping Git tests"
fi
