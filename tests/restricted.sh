export NIX_TEST_ROOT="$(cd -P -- "$(dirname -- "$0")" && pwd -P)"
source "$NIX_TEST_ROOT/common.sh"

setupTest

clearStore

nix_instantiate="nix-instantiate --option restrict-eval true"

cd "$NIX_TEST_ROOT"

$nix_instantiate --eval -E '1 + 2'
! $nix_instantiate "simple.nix"
$nix_instantiate "simple.nix" -I src="."
$nix_instantiate "simple.nix" \
	-I src1="simple.nix" \
	-I src2="config.nix" \
	-I src3="simple.builder.sh"

! $nix_instantiate --eval -E 'builtins.readFile ./simple.nix'
#FIXME: $nix_instantiate --eval -E 'builtins.readFile simple.nix' -I src=..
#error: undefined variable ‘simple’ at (string):1:19

! $nix_instantiate --eval -E 'builtins.readDir ../src/boost'
$nix_instantiate --eval -E 'builtins.readDir ../src/boost' -I src=../src

! $nix_instantiate --eval -E 'let __nixPath = [ { prefix = "foo"; path = ./.; } ]; in <foo>'
$nix_instantiate --eval -E 'let __nixPath = [ { prefix = "foo"; path = ./.; } ]; in <foo>' -I src=.

