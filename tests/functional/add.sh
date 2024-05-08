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

#### New style commands

clearStore

(
    path1=$(nix store add ./dummy)
    path2=$(nix store add --mode nar ./dummy)
    path3=$(nix store add-path ./dummy)
    [[ "$path1" == "$path2" ]]
    [[ "$path1" == "$path3" ]]
    path4=$(nix store add --mode nar --hash-algo sha1 ./dummy)
)
(
    path1=$(nix store add --mode flat ./dummy)
    path2=$(nix store add-file ./dummy)
    [[ "$path1" == "$path2" ]]
    path4=$(nix store add --mode flat --hash-algo sha1 ./dummy)
)
(
    path1=$(nix store add --mode text ./dummy)
    path2=$(nix eval --impure --raw --expr 'builtins.toFile "dummy" (builtins.readFile ./dummy)')
    [[ "$path1" == "$path2" ]]
)
