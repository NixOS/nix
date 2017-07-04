export NIX_TEST_ROOT="$(cd -P -- "$(dirname -- "$0")" && pwd -P)"
source "$NIX_TEST_ROOT/common.sh"

setupTest

[ -v V ] && set -x

clearStore

outPath=$(nix-build $NIX_TEST_ROOT/build-hook.nix --no-out-link --option build-hook "$NIX_TEST_ROOT/build-hook.hook.sh")

echo "output path is $outPath"

text=$(cat "$outPath"/foobar)
test "$text" = "BARBAR"
