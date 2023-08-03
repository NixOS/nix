source common.sh

clearStore

# XXX: These invocations of nix-build should always return 100 according to the manpage, but often return 1
(! nix-build ./keep-going.nix -j2)
(! nix-build ./keep-going.nix -A good -j0) || \
    fail "Hello shouldn't have been built because of earlier errors"

clearStore

(! nix-build ./keep-going.nix --keep-going -j2)
nix-build ./keep-going.nix -A good -j0 || \
    fail "Hello should have been built despite the errors because of '--keep-going'"
