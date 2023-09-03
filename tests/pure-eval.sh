source common.sh

clearStore

nix eval --expr 'assert 1 + 2 == 3; true'

[[ $(nix eval --impure --expr 'builtins.readFile ./pure-eval.sh') =~ clearStore ]]

(! nix eval --expr 'builtins.readFile ./pure-eval.sh')

(! nix eval --expr builtins.currentTime)
(! nix eval --expr builtins.currentSystem)

(! nix-instantiate --pure-eval ./simple.nix)

[[ $(nix eval --impure --expr "(import (builtins.fetchurl { url = file://$(pwd)/pure-eval.nix; })).x") == 123 ]]
(! nix eval --expr "(import (builtins.fetchurl { url = file://$(pwd)/pure-eval.nix; })).x")
nix eval --expr "(import (builtins.fetchurl { url = file://$(pwd)/pure-eval.nix; sha256 = \"$(nix hash file pure-eval.nix --type sha256)\"; })).x"

rm -rf $TEST_ROOT/eval-out
nix eval --store dummy:// --write-to $TEST_ROOT/eval-out --expr '{ x = "foo" + "bar"; y = { z = "bla"; }; }'
[[ $(cat $TEST_ROOT/eval-out/x) = foobar ]]
[[ $(cat $TEST_ROOT/eval-out/y/z) = bla ]]

rm -rf $TEST_ROOT/eval-out
(! nix eval --store dummy:// --write-to $TEST_ROOT/eval-out --expr '{ "." = "bla"; }')
