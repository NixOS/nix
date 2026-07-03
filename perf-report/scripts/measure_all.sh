#!/usr/bin/env bash
: "${SCRATCH:=/tmp/nix-perf}"   # override with: export SCRATCH=/path
set -euo pipefail
S=${SCRATCH}
BASE=$S/build-base/src/nix/nix
INTERP=$S/build-interp/src/nix/nix
MASTER=$S/nix-master/bin/nix                 # 2.35.0pre, nixpkgs-built (has interp flags)
OLD=/home/siraben/.nix-profile/bin/nix       # 2.32.1

echo "########## CONFIRMATION (interleaved, order-reversed) ##########"
bash $S/stat.sh $INTERP impure 15 "B2. interp  (run first this round)"
bash $S/stat.sh $BASE   impure 15 "A2. baseline (run second this round)"

echo "########## PURE vs IMPURE (same binary: 2.35 master) ##########"
bash $S/stat.sh $MASTER impure 15 "impure  (<nixpkgs>, filesystem accessor)"
bash $S/stat.sh $MASTER pure   15 "pure    (flake, store accessor + hashing)"

echo "########## VERSION: 2.32.1 vs 2.35 master (impure, identical nixpkgs) ##########"
bash $S/stat.sh $OLD    impure 15 "2.32.1 (installed)"
bash $S/stat.sh $MASTER impure 15 "2.35.0-master"
echo "MEASURE_ALL_DONE"
