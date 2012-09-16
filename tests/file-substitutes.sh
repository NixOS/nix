source common.sh

clearStore

export NIX_SUBSTITUTERS=$(pwd)/file-substituter.sh

startDaemon

evalFile1=`nix-instantiate --eval-only -A imported.evalFile1 $(pwd)/file-substitutes.nix`
evalFile2=`nix-instantiate --eval-only -A imported.evalFile2 $(pwd)/file-substitutes.nix`
coerceToString1=`nix-instantiate --eval-only -A imported.coerceToString1 $(pwd)/file-substitutes.nix`
coerceToString2=`nix-instantiate --eval-only -A imported.coerceToString2 $(pwd)/file-substitutes.nix`

test "$evalFile1" = "$evalFile2"
test "$evalFile1" = "true"
