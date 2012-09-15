source common.sh

clearStore

export NIX_SUBSTITUTERS=$(pwd)/file-substituter.sh

startDaemon

evalFile1=`nix-instantiate --eval-only -A imported.evalFile1 $(pwd)/file-substitutes.nix`
evalFile2=`nix-instantiate --eval-only -A imported.evalFile2 $(pwd)/file-substitutes.nix`
coerceToString1=`nix-instantiate --eval-only -A imported.coerceToString1 $(pwd)/file-substitutes.nix`
coerceToString2=`nix-instantiate --eval-only -A imported.coerceToString2 $(pwd)/file-substitutes.nix`
isValidPath1=`nix-instantiate --eval-only -A imported.isValidPath1 $(pwd)/file-substitutes.nix`
isValidPath2=`nix-instantiate --eval-only -A imported.isValidPath2 $(pwd)/file-substitutes.nix`
pathExists1=`nix-instantiate --eval-only -A imported.pathExists1 $(pwd)/file-substitutes.nix`
pathExists2=`nix-instantiate --eval-only -A imported.pathExists2 $(pwd)/file-substitutes.nix`
readFile1=`nix-instantiate --eval-only -A imported.readFile1 $(pwd)/file-substitutes.nix`
readFile2=`nix-instantiate --eval-only -A imported.readFile2 $(pwd)/file-substitutes.nix`
filterSource1=`nix-instantiate --eval-only -A imported.filterSource1 $(pwd)/file-substitutes.nix`
filterSource2=`nix-instantiate --eval-only -A imported.filterSource2 $(pwd)/file-substitutes.nix`

test "$evalFile1" = "$evalFile2"
test "$evalFile1" = "true"
test "$coerceToString1" = "$coerceToString2"
test "$isValidPath1" = "$isValidPath2"
test "$pathExists1" = "$pathExists2"
test "$pathExists1" = "true"
test "$readFile1" = "$readFile2"
test "$readFile1" = "\"A file\""
test "$filterSource1" = "$filterSource2"
