#!/usr/bin/env bash

source common.sh

clearStoreIfPossible

cp ./dependencies.nix ./dependencies.builder0.sh "${config_nix}" "$TEST_HOME"

cd "$TEST_HOME"

nix why-depends --derivation --file ./dependencies.nix input2_drv input1_drv
nix why-depends --file ./dependencies.nix input2_drv input1_drv

nix-build ./dependencies.nix -A input0_drv -o dep
nix-build ./dependencies.nix -o toplevel

FAST_WHY_DEPENDS_OUTPUT=$(nix why-depends ./toplevel ./dep)
PRECISE_WHY_DEPENDS_OUTPUT=$(nix why-depends ./toplevel ./dep --precise)

# Both outputs should show that `input-2` is in the dependency chain
echo "$FAST_WHY_DEPENDS_OUTPUT" | grepQuiet input-2
echo "$PRECISE_WHY_DEPENDS_OUTPUT" | grepQuiet input-2

# But only the “precise” one should refer to `reference-to-input-2`
echo "$FAST_WHY_DEPENDS_OUTPUT" | grepQuietInverse reference-to-input-2
echo "$PRECISE_WHY_DEPENDS_OUTPUT" | grepQuiet reference-to-input-2

<<<"$PRECISE_WHY_DEPENDS_OUTPUT" sed -n '2p' | grepQuiet "└───reference-to-input-2 -> "
<<<"$PRECISE_WHY_DEPENDS_OUTPUT" sed -n '3p' | grep "    →" | grepQuiet "dependencies-input-2"
<<<"$PRECISE_WHY_DEPENDS_OUTPUT" sed -n '4p' | grepQuiet "    └───input0: …"                          # in input-2, file input0
<<<"$PRECISE_WHY_DEPENDS_OUTPUT" sed -n '5p' | grep "        →" | grepQuiet "dependencies-input-0"    # is dependencies-input-0 referenced
