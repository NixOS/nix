source common.sh

clearStore

cp ./dependencies.nix ./dependencies.builder0.sh ./config.nix $TEST_HOME

cd $TEST_HOME

nix-build ./dependencies.nix -A input0_drv -o dep
nix-build ./dependencies.nix -o toplevel

FAST_WHY_DEPENDS_OUTPUT=$(nix why-depends ./toplevel ./dep)
PRECISE_WHY_DEPENDS_OUTPUT=$(nix why-depends ./toplevel ./dep --precise)

# Both outputs should show that `input-2` is in the dependency chain
echo "$FAST_WHY_DEPENDS_OUTPUT" | grep -q input-2
echo "$PRECISE_WHY_DEPENDS_OUTPUT" | grep -q input-2

# But only the “precise” one should refere to `reference-to-input-2`
echo "$FAST_WHY_DEPENDS_OUTPUT" | (! grep -q reference-to-input-2)
echo "$PRECISE_WHY_DEPENDS_OUTPUT" | grep -q reference-to-input-2
