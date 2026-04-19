#!/usr/bin/env bash

source common.sh

clearStoreIfPossible
clearCache

(( $(nix search -f search.nix '' hello | wc -l) > 0 ))

# Check descriptions are searched
(( $(nix search -f search.nix '' broken | wc -l) > 0 ))

# Check search that matches nothing
(( $(nix search -f search.nix '' nosuchpackageexists | wc -l) == 0 ))

# Search for multiple arguments
(( $(nix search -f search.nix '' hello empty | wc -l) == 2 ))

# Multiple arguments will not exist
(( $(nix search -f search.nix '' hello broken | wc -l) == 0 ))

# No regex should return an error
(( $(nix search -f search.nix '' | wc -l) == 0 ))

## Search expressions

# Check that empty search string matches all
nix search -f search.nix '' ^ | grepQuiet foo
nix search -f search.nix '' ^ | grepQuiet bar
nix search -f search.nix '' ^ | grepQuiet hello

## Tests for multiple regex/match highlighting

e=$'\x1b' # grep doesn't support \e, \033 or even \x1b
# Multiple overlapping regexes
(( $(nix search -f search.nix '' 'oo' 'foo' 'oo' | grep -c "$e\[32;1mfoo$e\\[0;1m") == 1 ))
(( $(nix search -f search.nix '' 'broken b' 'en bar' | grep -c "$e\[32;1mbroken bar$e\\[0m") == 1 ))

# Multiple matches
# Searching for 'o' should yield the 'o' in 'broken bar', the 'oo' in foo and 'o' in hello
(( $(nix search -f search.nix '' 'o' | grep -Eoc "$e\[32;1mo{1,2}$e\[(0|0;1)m") == 3 ))
# Searching for 'b' should yield the 'b' in bar and the two 'b's in 'broken bar'
# NOTE: This does not work with `grep -c` because it counts the two 'b's in 'broken bar' as one matched line
(( $(nix search -f search.nix '' 'b' | grep -Eo "$e\[32;1mb$e\[(0|0;1)m" | wc -l) == 3 ))

## Tests for --exclude
(( $(nix search -f search.nix ^ -e hello | grep -c hello) == 0 ))

(( $(nix search -f search.nix foo ^ --exclude 'foo|bar' | grep -Ec 'foo|bar') == 0 ))
(( $(nix search -f search.nix foo ^ -e foo --exclude bar | grep -Ec 'foo|bar') == 0 ))
[[ $(nix search -f search.nix '' ^ -e bar --json | jq -c 'keys') == '["foo","hello"]' ]]

## Tests for --calc-derivation

# Check that --calc-derivation adds both outPath and drvPath to JSON output
[[ -n $(nix search -f search.nix '' hello --json --calc-derivation | jq -r '.hello.outPath') ]]
[[ -n $(nix search -f search.nix '' hello --json --calc-derivation | jq -r '.hello.drvPath') ]]

# Check that without --calc-derivation, paths are not present
[[ $(nix search -f search.nix '' hello --json | jq 'has("hello") and (.hello | has("outPath") | not) and (.hello | has("drvPath") | not)') == 'true' ]]

# Check that other fields are still present with --calc-derivation
[[ $(nix search -f search.nix '' hello --json --calc-derivation | jq 'has("hello") and (.hello | has("pname")) and (.hello | has("version")) and (.hello | has("description"))') == 'true' ]]

## Tests for --json-lines

# Check that --json-lines outputs one JSON object per line
(( $(nix search -f search.nix '' ^ --json-lines | wc -l) == 3 ))

# Check that each line is valid JSON
nix search -f search.nix '' hello --json-lines | jq -e 'has("hello")'

# Check that --json-lines works with --calc-derivation
[[ -n $(nix search -f search.nix '' hello --json-lines --calc-derivation | jq -r '.hello.outPath') ]]

## Tests for --apply

# Check that --apply works for simple transformations
[[ $(nix search -f search.nix '' hello --apply 'drv: { drvName = drv.name; }' | jq -r '.hello.drvName') == 'hello-0.1' ]]

# Check that --apply can access outPath
[[ -n $(nix search -f search.nix '' hello --apply 'drv: { out = drv.outPath; }' | jq -r '.hello.out') ]]

# Check that --apply can transform derivation attributes
[[ $(nix search -f search.nix '' hello --apply 'drv: { name = drv.name; type = drv.type; }' | jq 'has("hello") and (.hello | has("name")) and (.hello | has("type"))') == 'true' ]]

## Tests for --check-cache

# Check that --check-cache adds cached field
[[ $(nix search -f search.nix '' hello --json --check-cache | jq 'has("hello") and (.hello | has("cached"))') == 'true' ]]

# Check that cached field is a boolean
[[ $(nix search -f search.nix '' hello --json --check-cache | jq '.hello.cached | type') == '"boolean"' ]]
