source common.sh

drvPath=$(nix-instantiate simple.nix)

nix derivation show $drvPath | jq .[] > $TEST_HOME/simple.json

drvPath2=$(nix derivation add < $TEST_HOME/simple.json)

[[ "$drvPath" = "$drvPath2" ]]

# Input addressed derivations cannot be renamed.
jq '.name = "foo"' < $TEST_HOME/simple.json | expectStderr 1 nix derivation add | grepQuiet "has incorrect output"
