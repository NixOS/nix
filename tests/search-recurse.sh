source common.sh

clearStore
clearCache

(( $(nix search --argstr foo unused -f search-recurse.nix '' inner | wc -l) > 0 ))
(( $(nix search --argstr foo unused -f search-recurse.nix '' hidden | wc -l) == 0 ))
(( $(nix search --impure --expr 'import ./search-recurse.nix { foo = "unused"; }' '' inner | wc -l) > 0 ))

# If missing auto-call args, the error message should mention '--arg'
(! nix search -f search-recurse.nix '') |& grep -q -- '--arg'

nix search --argstr foo unused -f search-recurse.nix nestRecurse |& grep -q 'nestRecurse.inner'
nix search --argstr foo unused -f search-recurse.nix nestNoRecurse |& grep -q 'nestNoRecurse.hidden'
