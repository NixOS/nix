source common.sh

clearStore

nix eval '(assert 1 + 2 == 3; true)'

[[ $(nix eval --impure '(builtins.readFile ./pure-eval.sh)') =~ clearStore ]]

(! nix eval '(builtins.readFile ./pure-eval.sh)')

(! nix eval '(builtins.currentTime)')
(! nix eval '(builtins.currentSystem)')

(! nix-instantiate --pure-eval ./simple.nix)

[[ $(nix eval --impure "((import (builtins.fetchurl { url = file://$(pwd)/pure-eval.nix; })).x)") == 123 ]]
(! nix eval "((import (builtins.fetchurl { url = file://$(pwd)/pure-eval.nix; })).x)")
nix eval "((import (builtins.fetchurl { url = file://$(pwd)/pure-eval.nix; sha256 = \"$(nix hash-file pure-eval.nix --type sha256)\"; })).x)"
