source common.sh

drvPath=$(nix-instantiate recursive.nix)

test "$(nix-store -q --binding system "$drvPath")" = "$system"

echo "derivation is $drvPath"

outPath=$(nix-store -rvv "$drvPath")

echo "output path is $outPath"

text=$(cat "$outPath"/hello)
if test "$text" != "Hello World!"; then exit 1; fi

# Directed delete: $outPath is not reachable from a root, so it should
# be deleteable.
nix-store --delete $outPath
if test -e $outPath/hello; then false; fi
