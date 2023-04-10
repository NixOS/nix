#!/usr/bin/env bash
set -eux
LOG_FILE=/tmp/repro-7998.log
rm -f "$LOG_FILE"
for i in {1..8}; do
  (
    while true; do
      nix-build \
        --argstr uuid $(uuidgen) \
        --arg drvCount $((RANDOM % 256)) \
        -E '
          { uuid ? "00000000-0000-0000-0000-000000000000", drvCount ? 0 }:
          with import <nixpkgs> { };
          let
            mkDrv = name: buildInputs:
              stdenv.mkDerivation {
                inherit name;
                inherit buildInputs;
                unpackPhase = "date +\"${uuid} %F %T\" >date.txt";
                installPhase = "mkdir -p $out; cp date.txt $out/";
              };
            mkDrvs = n:
              let
                name = "repro-7998-${toString n}";
                buildInputs = if n == 0 then [ ] else [ (mkDrvs (n - 1)) ];
              in mkDrv name buildInputs;
          in mkDrvs drvCount
        '
    done 2>&1 | tee -a "$LOG_FILE"
  ) &
done
read # Press enter to stop
pkill -KILL -f repro-7998.sh
