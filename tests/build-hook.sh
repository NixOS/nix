source common.sh

clearStore

outPath=$(nix-build build-hook.nix --no-out-link --option build-hook $(pwd)/build-hook.hook.sh)

echo "output path is $outPath"

text=$(cat "$outPath"/foobar)
if test "$text" != "BARBAR"; then exit 1; fi
