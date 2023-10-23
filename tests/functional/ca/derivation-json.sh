source common.sh

export NIX_TESTS_CA_BY_DEFAULT=1

drvPath=$(nix-instantiate ../simple.nix)

nix derivation show $drvPath | jq .[] > $TEST_HOME/simple.json

drvPath2=$(nix derivation add < $TEST_HOME/simple.json)

[[ "$drvPath" = "$drvPath2" ]]

# Content-addressed derivations can be renamed.
jq '.name = "foo"' < $TEST_HOME/simple.json > $TEST_HOME/foo.json
drvPath3=$(nix derivation add --dry-run < $TEST_HOME/foo.json)
# With --dry-run nothing is actually written
[[ ! -e "$drvPath3" ]]

# But the JSON is rejected without the experimental feature
expectStderr 1 nix derivation add < $TEST_HOME/foo.json --experimental-features nix-command | grepQuiet "experimental Nix feature 'ca-derivations' is disabled"

# Without --dry-run it is actually written
drvPath4=$(nix derivation add < $TEST_HOME/foo.json)
[[ "$drvPath4" = "$drvPath3" ]]
[[ -e "$drvPath3" ]]

# The modified derivation read back as JSON matches
nix derivation show $drvPath3 | jq .[] > $TEST_HOME/foo-read.json
diff $TEST_HOME/foo.json $TEST_HOME/foo-read.json
