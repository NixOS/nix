#!/usr/bin/env bash

# FIXME: Floating content-addressed derivations are version 2 derivations;
# these characterisation tests need version 2 counterparts.
echo "skipping: no version 2 counterparts yet" >&2
exit 77

export NIX_TESTS_CA_BY_DEFAULT=1

cd ..
source derivation-advanced-attributes.sh
