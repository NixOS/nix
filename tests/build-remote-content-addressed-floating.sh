source common.sh

file=build-hook-ca-floating.nix

enableFeatures "ca-derivations ca-references"

CONTENT_ADDRESSED=true

source build-remote.sh
