# shellcheck shell=bash

testReplResponse '
drvPath
' '".*-simple.drv"' \
    --file "$_NIX_TEST_SOURCE_DIR/simple.nix"

testReplResponse '
drvPath
' '".*-simple.drv"' \
    --file "$_NIX_TEST_SOURCE_DIR/simple.nix" --experimental-features 'ca-derivations'

# `--file` autocalls, `--expr` does not.
testReplResponse '
drvPath
' '".*-simple.drv"' \
    --file "./function.nix"

testReplResponse '
' 'while evaluating an attribute set to be merged in the global scope' \
    --expr '{ x ? 1 }: import '"$_NIX_TEST_SOURCE_DIR"'/simple.nix'

# Don't prompt for more input when getting unexpected EOF in imported files.
testReplResponse "
import $_NIX_TEST_SOURCE_DIR/lang/parse-fail-eof-pos.nix
" \
    '.*error: syntax error, unexpected end of file.*'
