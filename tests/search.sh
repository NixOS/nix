source common.sh

clearStore
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

## Search expressions

# Check that empty search string matches all
nix search -f search.nix '' |grep -q foo
nix search -f search.nix '' |grep -q bar
nix search -f search.nix '' |grep -q hello

## Tests for multiple regex/match highlighting

e=$'\x1b' # grep doesn't support \e, \033 or even \x1b
# Multiple overlapping regexes
(( $(nix search -f search.nix '' 'oo' 'foo' 'oo' | grep "$e\[32;1mfoo$e\\[0;1m" | wc -l) == 1 ))
(( $(nix search -f search.nix '' 'broken b' 'en bar' | grep "$e\[32;1mbroken bar$e\\[0m" | wc -l) == 1 ))

# Multiple matches
# Searching for 'o' should yield the 'o' in 'broken bar', the 'oo' in foo and 'o' in hello
(( $(nix search -f search.nix '' 'o' | grep -Eo "$e\[32;1mo{1,2}$e\[(0|0;1)m" | wc -l) == 3 ))
# Searching for 'b' should yield the 'b' in bar and the two 'b's in 'broken bar'
(( $(nix search -f search.nix '' 'b' | grep -Eo "$e\[32;1mb$e\[(0|0;1)m" | wc -l) == 3 ))
