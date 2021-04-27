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
