source common.sh

file=build-hook-ca-floating.nix

enableFeatures "ca-derivations"

CONTENT_ADDRESSED=true

source build-remote.sh
