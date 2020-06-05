source common.sh

clearStore

nix eval --pure-eval '(assert 1 + 2 == 3; true)'

[[ $(nix eval '(builtins.readFile ./pure-eval.sh)') =~ clearStore ]]

(! nix eval --pure-eval '(builtins.readFile ./pure-eval.sh)')

(! nix eval --pure-eval '(builtins.currentTime)')
(! nix eval --pure-eval '(builtins.currentSystem)')

(! nix-instantiate --pure-eval ./simple.nix)

[[ $(nix eval "((import (builtins.fetchurl { url = file://$(pwd)/pure-eval.nix; })).x)") == 123 ]]
(! nix eval --pure-eval "((import (builtins.fetchurl { url = file://$(pwd)/pure-eval.nix; })).x)")
nix eval --pure-eval "((import (builtins.fetchurl { url = file://$(pwd)/pure-eval.nix; sha256 = \"$(nix hash-file pure-eval.nix --type sha256)\"; })).x)"
