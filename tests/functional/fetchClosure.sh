#!/usr/bin/env bash

source common.sh

enableFeatures "fetch-closure"

TODO_NixOS

clearStore
clearCacheCache

# Old daemons don't properly zero out the self-references when
# calculating the CA hashes, so this breaks `nix store
# make-content-addressed` which expects the client and the daemon to
# compute the same hash
requireDaemonNewerThan "2.16.0pre20230524"

# Initialize binary cache.
nonCaPath=$(nix build --json --file ./dependencies.nix --no-link | jq -r .[].outputs.out)
caPath=$(nix store make-content-addressed --json "$nonCaPath" | jq -r '.rewrites | map(.) | .[]')
nix copy --to file://"$cacheDir" "$nonCaPath"

# Test basic fetchClosure rewriting from non-CA to CA.
clearStore

[ ! -e "$nonCaPath" ]
[ ! -e "$caPath" ]

[[ $(nix eval -v --raw --expr "
  builtins.fetchClosure {
    fromStore = \"file://$cacheDir\";
    fromPath = $nonCaPath;
    toPath = $caPath;
  }
") = "$caPath" ]]

[ ! -e "$nonCaPath" ]
[ -e "$caPath" ]

clearStore

# The daemon will reject input addressed paths unless configured to trust the
# cache key or the user. This behavior should be covered by another test, so we
# skip this part when using the daemon.
if [[ "$NIX_REMOTE" != "daemon" ]]; then

    # If we want to return a non-CA path, we have to be explicit about it.
    expectStderr 1 nix eval --raw --no-require-sigs --expr "
      builtins.fetchClosure {
        fromStore = \"file://$cacheDir\";
        fromPath = $nonCaPath;
      }
    " | grepQuiet -E "The .fromPath. value .* is input-addressed, but .inputAddressed. is set to .false."

    # TODO: Should the closure be rejected, despite single user mode?
    # [ ! -e $nonCaPath ]

    [ ! -e "$caPath" ]

    # We can use non-CA paths when we ask explicitly.
    [[ $(nix eval --raw --no-require-sigs --expr "
      builtins.fetchClosure {
        fromStore = \"file://$cacheDir\";
        fromPath = $nonCaPath;
        inputAddressed = true;
      }
    ") = "$nonCaPath" ]]

    [ -e "$nonCaPath" ]
    [ ! -e "$caPath" ]


fi

[ ! -e "$caPath" ]

# 'toPath' set to empty string should fail but print the expected path.
expectStderr 1 nix eval -v --json --expr "
  builtins.fetchClosure {
    fromStore = \"file://$cacheDir\";
    fromPath = $nonCaPath;
    toPath = \"\";
  }
" | grep "error: rewriting.*$nonCaPath.*yielded.*$caPath"

# If fromPath is CA, then toPath isn't needed.
nix copy --to file://"$cacheDir" "$caPath"

clearStore

[ ! -e "$caPath" ]

[[ $(nix eval -v --raw --expr "
  builtins.fetchClosure {
    fromStore = \"file://$cacheDir\";
    fromPath = $caPath;
  }
") = "$caPath" ]]

[ -e "$caPath" ]

# Test import-from-derivation on the result of fetchClosure.
[[ $(nix eval -v --expr "
  import \"\${builtins.fetchClosure {
    fromStore = \"file://$cacheDir\";
    fromPath = $caPath;
  }}/foo.nix\"
") = 3 ]]

# Check that URL query parameters aren't allowed.
clearStore
narCache=$TEST_ROOT/nar-cache
rm -rf "$narCache"
(! nix eval -v --raw --expr "
  builtins.fetchClosure {
    fromStore = \"file://$cacheDir?local-nar-cache=$narCache\";
    fromPath = $caPath;
  }
")
# shellcheck disable=SC2235
(! [ -e "$narCache" ])

# If toPath is specified but wrong, we check it (only) when the path is missing.
clearStore

# shellcheck disable=SC2001
badPath=$(echo "$caPath" | sed -e 's!/store/................................-!/store/xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx-!')

[ ! -e "$badPath" ]

expectStderr 1 nix eval -v --raw --expr "
  builtins.fetchClosure {
    fromStore = \"file://$cacheDir\";
    fromPath = $nonCaPath;
    toPath = $badPath;
  }
" | grep "error: rewriting.*$nonCaPath.*yielded.*$caPath.*while.*$badPath.*was expected"

[ ! -e "$badPath" ]

# We only check it when missing, as a performance optimization similar to what we do for fixed output derivations. So if it's already there, we don't check it.
# It would be nice for this to fail, but checking it would be too(?) slow.
[ -e "$caPath" ]

[[ $(nix eval -v --raw --expr "
  builtins.fetchClosure {
    fromStore = \"file://$cacheDir\";
    fromPath = $badPath;
    toPath = $caPath;
  }
") = "$caPath" ]]


# However, if the output address is unexpected, we can report it


expectStderr 1 nix eval -v --raw --expr "
  builtins.fetchClosure {
    fromStore = \"file://$cacheDir\";
    fromPath = $caPath;
    inputAddressed = true;
  }
" | grepQuiet 'error.*The store object referred to by.*fromPath.* at .* is not input-addressed, but .*inputAddressed.* is set to .*true.*'

