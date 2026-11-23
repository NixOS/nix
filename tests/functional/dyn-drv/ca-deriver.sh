# shellcheck shell=bash
source ../common.sh

# Need daemon to support the `realisation-with-path-not-hash` protocol
# feature (build trace rework). Without it, queryRealisation returns
# nullptr and CA/dynamic derivation output lookup fails.
requireDaemonNewerThan "2.35.0pre20260303"

enableFeatures "ca-derivations dynamic-derivations recursive-nix"

TODO_NixOS

restartDaemon

# From https://github.com/NixOS/nix/issues/16823

deriver=$(nix-store -qd "$(nix build -f ./ca-deriver.nix --print-out-paths --no-link)")

if [[ "$deriver" == *unknown-deriver* ]]; then
    exit 1
fi
