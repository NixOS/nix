source common.sh

drvPath=$(nix-instantiate simple.nix)

function removeDerivationsFromStore () {
    # Delete derivations until none are left. It must be done like this as deletion
    # fails if another .drv in the store depends on the one to be deleted.
    until [ -z "$(ls $NIX_STORE_DIR/*.drv)" ]; do
        find $NIX_STORE_DIR -name "*.drv" -exec nix store delete {} \;
    done
}

# Simple roundtrip export and import
nix derivation show $drvPath > $TEST_HOME/simple.json
removeDerivationsFromStore
drvPath2=$(nix derivation add < $TEST_HOME/simple.json)
[[ "$drvPath" = "$drvPath2" ]]

# Unwrapped single derivation is also supported
cat $TEST_HOME/simple.json | jq .[] > $TEST_HOME/simple-unwrapped.json
removeDerivationsFromStore
drvPath3=$(nix derivation add < $TEST_HOME/simple-unwrapped.json)
[[ "$drvPath" = "$drvPath3" ]]

# Input addressed derivations cannot be renamed.
removeDerivationsFromStore
cat $TEST_HOME/simple-unwrapped.json | jq '.name = "foo"' | expectStderr 1 nix derivation add | grepQuiet "has incorrect output"

# Test recursive show and add with dependencies
topDrvPath=$(nix-instantiate dependencies.nix)

nix derivation show -r $topDrvPath > $TEST_HOME/depend.json
[[ $(jq length $TEST_HOME/depend.json) = 4 ]]
removeDerivationsFromStore
dependPaths=$(nix derivation add < $TEST_HOME/depend.json | sort)
[[ "$dependPaths" = "$(jq -r 'keys | .[]' $TEST_HOME/depend.json | sort)" ]]

# Error traces when adding
diff -u <(
    cat $TEST_HOME/simple.json \
    | jq 'map_values( .outputs = { out: .env.out } )' \
    | nix derivation add 2>&1 \
    | grep -A 10 -e '^error:' \
    || true
) <(cat <<EOF
error:
       … while reading JSON derivation with key '$drvPath'

       … while reading key 'outputs'

       error: Expected JSON value to be of type 'object' but it is of type 'string'
EOF
)

# nix derivation add can only add derivations that are part of its JSON input
# If an inputDrv of one derivation is missing and not already valid in the store, it will fail
function findDrvName() {
    echo "$(jq -r "to_entries[] | select(.key | endswith(\"$1\")) | .key | split(\"/\") | last" $TEST_HOME/depend.json)"
}
drvInput0=$(findDrvName "input-0.drv")
drvInput1=$(findDrvName "input-1.drv")
drvInput2=$(findDrvName "input-2.drv")
drvTop=$(findDrvName "top.drv")

removeDerivationsFromStore
output="$(
    cat $TEST_HOME/depend.json \
    | jq "del(.[\"$NIX_STORE_DIR/$drvInput0\"])" \
    | nix derivation add 2>&1 \
    | grep -A 20 -e '^error:' \
    || true
)"
head -1 <<< $output | grepQuiet -Fs "error: Missing inputs:";

missingInputs="$(head -3 <<< $output | tail -2)"
grepQuiet -Fs "       '$drvTop' requires '$drvInput0', but it is not in the input JSON or the Nix Store" <<< $missingInputs
grepQuiet -Fs "       '$drvInput2' requires '$drvInput0', but it is not in the input JSON or the Nix Store" <<< $missingInputs

diff -u <(tail -3 <<< $output) <(cat <<EOF
error: Some inputs are missing, so the derivations can't be added.
       - All required derivations must be in the store or the JSON input.
         You may want to re-export the JSON with 'nix derivation show -r'.
EOF
);

# nix derivation add can only add derivations. If an inputSrc is missing, it will fail
removeDerivationsFromStore
missingInputSrc="$(jq -r 'to_entries[] | select(.key | endswith("input-2.drv")) | .value.inputSrcs[0] | split ("/") | last' $TEST_HOME/depend.json)"
nix store delete "$NIX_STORE_DIR/$missingInputSrc"
output="$(
    cat $TEST_HOME/depend.json \
    | nix derivation add 2>&1 \
    | grep -A 20 -e '^error:' \
    || true
)"
head -1 <<< $output | grepQuiet -Fs "error: Missing inputs:";

missingInputs="$(head -2 <<< $output | tail -1)"
grepQuiet -Fs "       '$drvInput2' requires '$missingInputSrc', but it is not present in the Nix Store" <<< $missingInputs

diff -u <(tail -3 <<< $output) <(cat <<EOF
error: Some inputs are missing, so the derivations can't be added.
       - 'nix derivation add' can only add derivations, not sources.
         To easily transfer multiple sources from one store to another, use 'nix copy'.
EOF
)

# If both inputSrcs and inputDrvs are missing, they will be reported together
removeDerivationsFromStore
output="$(
    cat $TEST_HOME/depend.json \
    | jq "del(.[\"$NIX_STORE_DIR/$drvInput0\"])" \
    | nix derivation add 2>&1 \
    | grep -A 20 -e '^error:' \
    || true
)"
head -1 <<< $output | grepQuiet -Fs "error: Missing inputs:";

missingInputs="$(head -4 <<< $output | tail -3)"
grepQuiet -Fs "       '$drvInput2' requires '$missingInputSrc', but it is not present in the Nix Store" <<< $missingInputs
grepQuiet -Fs "       '$drvTop' requires '$drvInput0', but it is not in the input JSON or the Nix Store" <<< $missingInputs
grepQuiet -Fs "       '$drvInput2' requires '$drvInput0', but it is not in the input JSON or the Nix Store" <<< $missingInputs

diff -u <(tail -5 <<< $output) <(cat <<EOF
error: Some inputs are missing, so the derivations can't be added.
       - 'nix derivation add' can only add derivations, not sources.
         To easily transfer multiple sources from one store to another, use 'nix copy'.
       - All required derivations must be in the store or the JSON input.
         You may want to re-export the JSON with 'nix derivation show -r'.
EOF
);
