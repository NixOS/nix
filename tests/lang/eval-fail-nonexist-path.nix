# This must fail to evaluate, since ./fnord doesn't exist.  If it did
# exist, it would produce "/nix/store/<hash>-fnord/xyzzy" (with an
# appropriate context).
"${./fnord}/xyzzy"
