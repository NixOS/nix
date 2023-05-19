source common.sh

enableFeatures "fetch-closure"

clearStore
clearCacheCache

# Old daemons don't properly zero out the self-references when
# calculating the CA hashes, so this breaks `nix store
# make-content-addressed` which expects the client and the daemon to
# compute the same hash
requireDaemonNewerThan "2.16.0pre20230524"

# Initialize binary cache.
nonCaPath=$(nix build --json --file ./dependencies.nix --no-link | jq -r .[].outputs.out)
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

if [[ "$NIX_REMOTE" != "daemon" ]]; then

    # We can use non-CA paths when we ask explicitly.
    [[ $(nix eval --raw --no-require-sigs --expr "
      builtins.fetchClosure {
        fromStore = \"file://$cacheDir\";
        fromPath = $nonCaPath;
        inputAddressed = true;
      }
    ") = $nonCaPath ]]

    [ -e $nonCaPath ]

    # .. but only if we ask explicitly.
    expectStderr 1 nix eval --raw --no-require-sigs --expr "
      builtins.fetchClosure {
        fromStore = \"file://$cacheDir\";
        fromPath = $nonCaPath;
      }
    " | grepQuiet -E "The .fromPath. value .* is input addressed, but input addressing was not requested. If you do intend to return an input addressed store path, add .inputAddressed = true;. to the .fetchClosure. arguments."

    [ -e $nonCaPath ]


fi

# 'toPath' set to empty string should fail but print the expected path.
expectStderr 1 nix eval -v --json --expr "
  builtins.fetchClosure {
    fromStore = \"file://$cacheDir\";
    fromPath = $nonCaPath;
    toPath = \"\";
  }
" | grep "error: rewriting.*$nonCaPath.*yielded.*$caPath"

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
