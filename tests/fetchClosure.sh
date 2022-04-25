source common.sh

enableFeatures "fetch-closure"
needLocalStore "'--no-require-sigs' can’t be used with the daemon"

clearStore
clearCacheCache

# Initialize binary cache.
nonCaPath=$(nix build --json --file ./dependencies.nix | jq -r .[].outputs.out)
caPath=$(nix store make-content-addressed --json $nonCaPath | jq -r '.rewrites | map(.) | .[]')
nix copy --to file://$cacheDir $nonCaPath

# Test basic fetchClosure rewriting from non-CA to CA.
clearStore

[ ! -e $nonCaPath ]
[ ! -e $caPath ]

[[ $(nix eval -v --raw --expr "
  builtins.fetchClosure {
    fromStore = \"file://$cacheDir\";
    fromPath = $nonCaPath;
    toPath = $caPath;
  }
") = $caPath ]]

[ ! -e $nonCaPath ]
[ -e $caPath ]

# In impure mode, we can use non-CA paths.
[[ $(nix eval --raw --no-require-sigs --impure --expr "
  builtins.fetchClosure {
    fromStore = \"file://$cacheDir\";
    fromPath = $nonCaPath;
  }
") = $nonCaPath ]]

[ -e $nonCaPath ]

# 'toPath' set to empty string should fail but print the expected path.
nix eval -v --json --expr "
  builtins.fetchClosure {
    fromStore = \"file://$cacheDir\";
    fromPath = $nonCaPath;
    toPath = \"\";
  }
" 2>&1 | grep "error: rewriting.*$nonCaPath.*yielded.*$caPath"

# If fromPath is CA, then toPath isn't needed.
nix copy --to file://$cacheDir $caPath

[[ $(nix eval -v --raw --expr "
  builtins.fetchClosure {
    fromStore = \"file://$cacheDir\";
    fromPath = $caPath;
  }
") = $caPath ]]

# Check that URL query parameters aren't allowed.
clearStore
narCache=$TEST_ROOT/nar-cache
rm -rf $narCache
(! nix eval -v --raw --expr "
  builtins.fetchClosure {
    fromStore = \"file://$cacheDir?local-nar-cache=$narCache\";
    fromPath = $caPath;
  }
")
(! [ -e $narCache ])
