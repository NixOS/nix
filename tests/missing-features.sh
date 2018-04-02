source common.sh

clearStore

expectFailureMsg() {
    expect=$1
    shift
    if output=$("$@" 2>&1); then
        echo "Running '" "$@" "' succeeded, but it should not have!"
        exit 1
    fi

    echo "Expecting:"
    echo "$expect"
    echo ""
    echo "Received:"
    echo "$output"


    echo "$output" | grep "$expect"
}

expectFailureMsg \
    "a 'x86_64-bogus' system is required to build" \
    nix-build ./missing-features.nix -A zero

expectFailureMsg \
    "a 'x86_64-bogus' system with the feature 'bogus' is required to build" \
    nix-build ./missing-features.nix -A one

expectFailureMsg \
    "a 'x86_64-bogus' system with the features 'bogus', 'feature' is required to build" \
    nix-build ./missing-features.nix -A multiple
