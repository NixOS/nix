#!/usr/bin/env bash

# CA-derivation variant of `tests/functional/macho-rewrite.sh`. Same
# bug, same trigger, same assertions — but with `__contentAddressed =
# true; outputHashMode = "recursive"; outputHashAlgo = "sha256";`
# injected into the test fixture via the framework's
# `NIX_TESTS_CA_BY_DEFAULT=1` mechanism.
#
# This exists because issue NixOS/nix#6065 (which this fix closes) was
# filed against CA derivations specifically. The shared call site
# inside the `rewriteOutput` lambda at `derivation-builder.cc:~1651`
# covers both `InputAddressed` and `CAFloating`/`CAFixed`/`Impure`
# visitors, but a code-reading argument is weaker than a tested fact.

source common.sh

export NIX_TESTS_CA_BY_DEFAULT=1
cd ..
# shellcheck source=/dev/null
source ./macho-rewrite.sh
