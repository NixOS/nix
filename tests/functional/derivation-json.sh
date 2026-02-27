#!/usr/bin/env bash

source common.sh

drvPath=$(nix-instantiate simple.nix)

nix derivation show "$drvPath" | jq '.derivations[]' > "$TEST_HOME/simple.json"

# Round tripping to JSON works
drvPath2=$(nix derivation add < "$TEST_HOME/simple.json")
[[ "$drvPath" = "$drvPath2" ]]

# Derivation is input addressed, all outputs have a path
jq -e '.outputs | .[] | has("path")' < "$TEST_HOME/simple.json"

# Input addressed derivations cannot be renamed.
jq '.name = "foo"' < "$TEST_HOME/simple.json" | expectStderr 1 nix derivation add | grepQuiet "has incorrect output"

# If we remove the input addressed to make it a deferred derivation, we
# still get the same result because Nix will see that need not be
# deferred and fill in the right input address for us.
drvPath3=$(jq '.outputs |= map_values(del(.path))' < "$TEST_HOME/simple.json" | nix derivation add)
[[ "$drvPath" = "$drvPath3" ]]

# Test backward compatibility: JSON without 'meta' field should still be ingestible
drvPath4=$(jq 'del(.meta)' < "$TEST_HOME/simple.json" | nix derivation add)
[[ "$drvPath" = "$drvPath4" ]]

# Test that ingesting derivation with 'meta' field requires experimental feature
jq '.meta = {"description": "test"} | .structuredAttrs = {"requiredSystemFeatures": ["derivation-meta"]}' < "$TEST_HOME/simple.json" \
    | expectStderr 1 nix derivation add --experimental-features nix-command \
    | grepQuiet "experimental Nix feature 'derivation-meta' is disabled"
