source common.sh

clearStore

nix-instantiate --option restrict-eval true --eval -E '1 + 2'
(! nix-instantiate --option restrict-eval true ./simple.nix)
nix-instantiate --option restrict-eval true ./simple.nix -I src=.
nix-instantiate --option restrict-eval true ./simple.nix -I src1=simple.nix -I src2=config.nix -I src3=./simple.builder.sh

(! nix-instantiate --option restrict-eval true --eval -E 'builtins.readFile ./simple.nix')
nix-instantiate --option restrict-eval true --eval -E 'builtins.readFile ./simple.nix' -I src=..

(! nix-instantiate --option restrict-eval true --eval -E 'builtins.readDir ../src/boost')
nix-instantiate --option restrict-eval true --eval -E 'builtins.readDir ../src/boost' -I src=../src

(! nix-instantiate --option restrict-eval true --eval -E 'let __nixPath = [ { prefix = "foo"; path = ./.; } ]; in <foo>')
nix-instantiate --option restrict-eval true --eval -E 'let __nixPath = [ { prefix = "foo"; path = ./.; } ]; in <foo>' -I src=.

