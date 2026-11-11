#!/usr/bin/env bash

source common.sh

TODO_NixOS

RESULT=$TEST_ROOT/result
RESULT13out=$TEST_ROOT/result13out
RESULT13aux=$TEST_ROOT/result13aux
RESULT14aux=$TEST_ROOT/result14aux

dep=$(nix-build -o "$RESULT" check-refs.nix -A dep)

# test1 references dep, not itself.
test1=$(nix-build -o "$RESULT" check-refs.nix -A test1)
nix-store -q --references "$test1" | grepQuietInverse "$test1"
nix-store -q --references "$test1" | grepQuiet "$dep"

# test2 references src, not itself nor dep.
test2=$(nix-build -o "$RESULT" check-refs.nix -A test2)
nix-store -q --references "$test2" | grepQuietInverse "$test2"
nix-store -q --references "$test2" | grepQuietInverse "$dep"
nix-store -q --references "$test2" | grepQuiet aux-ref

# test3 should fail (unallowed ref).
(! nix-build -o "$RESULT" check-refs.nix -A test3)

# test4 should succeed.
nix-build -o "$RESULT" check-refs.nix -A test4

# test5 should succeed.
nix-build -o "$RESULT" check-refs.nix -A test5

# test6 should fail (unallowed self-ref).
(! nix-build -o "$RESULT" check-refs.nix -A test6)

# test7 should succeed (allowed self-ref).
nix-build -o "$RESULT" check-refs.nix -A test7

# test8 should fail (toFile depending on derivation output).
(! nix-build -o "$RESULT" check-refs.nix -A test8)

# test9 should fail (disallowed reference).
(! nix-build -o "$RESULT" check-refs.nix -A test9)

# test10 should succeed (no disallowed references).
nix-build -o "$RESULT" check-refs.nix -A test10

if ! isTestOnNixOS; then
    # If we have full control over our store, we can test some more things.

    if isDaemonNewer 2.12pre20230103; then
        if ! isDaemonNewer 2.16.0; then
            enableFeatures discard-references
            restartDaemon
        fi

        # test11 should succeed.
        test11=$(nix-build -o "$RESULT" check-refs.nix -A test11)
        [[ -z $(nix-store -q --references "$test11") ]]
    fi

fi

if isDaemonNewer "2.28pre20241225"; then
    # test12 should fail (syntactically invalid).
    expectStderr 1 nix-build -vvv -o "$RESULT" check-refs.nix -A test12 >"$TEST_ROOT/test12.stderr"
    if isDaemonNewer "2.33pre20251110"; then
        grepQuiet -F \
            "output check for 'lib' contains output name 'dev', but this is not a valid output of this derivation. (Valid outputs are [lib, out].)" \
            < "$TEST_ROOT/test12.stderr"
    else
        grepQuiet -F \
            "output check for 'lib' contains an illegal reference specifier 'dev', expected store path or output name (one of [lib, out])" \
            < "$TEST_ROOT/test12.stderr"
    fi
fi

if ! isTestOnNixOS; then
    # test13 and test14: a sibling output referenced by symbolic name in
    # outputChecks must remain resolvable, for the purpose of *this*
    # output's own checks, even when that sibling was already valid
    # before this build and therefore is not being (re-)registered here.
    # https://github.com/NixOS/nix/issues/16485

    # First build: both outputs are fresh, nothing pre-existing.
    clearStore
    outPath13=$(nix-build -o "$RESULT13out" check-refs.nix -A test13.out)
    auxPath13=$(nix-build -o "$RESULT13aux" check-refs.nix -A test13.aux)
    # nix-build suffixes a non-"out" output onto the given -o base, so the
    # symlink for the aux output above actually landed at "$RESULT13aux-aux".
    rm -f "$RESULT13out" "$RESULT13aux-aux"

    # Invalidate only aux. keep-outputs defaults to false in the test
    # sandbox, so this is enough to make aux (and only aux) invalid while
    # out remains a valid, unrooted path in the store.
    nix store delete "$auxPath13"
    [[ -e "$outPath13" ]] || fail "test13: out should still be valid after deleting aux"
    [[ ! -e "$auxPath13" ]] || fail "test13: aux should have been deleted"

    # Rebuilding aux reruns the builder for the whole derivation (as
    # always for a multi-output derivation), so out is rebuilt into a
    # scratch copy too -- but since the previously-registered out is
    # still valid, it takes the "already registered" path and is not
    # re-registered. aux's own disallowedReferences=["out"] check must
    # still be able to resolve "out" as a reference target in that case.
    nix-build -o "$RESULT13aux" check-refs.nix -A test13.aux
    rm -f "$RESULT13aux-aux"

    # test14 is the same shape, but aux genuinely references out, to
    # confirm this is still rejected -- i.e. that making "out" resolvable
    # as a symbolic reference target does not also disable enforcement of
    # the check against it. Unlike test13, this derivation's builder
    # *always* produces a real violation, so a normal first build of the
    # whole derivation can never succeed and can therefore never be the
    # source of an already-valid "out" (the build would fail before
    # registering anything, out included). To still reach the
    # asymmetric-validity condition -- out already valid, aux not --
    # register out's (statically known, input-addressed) path valid
    # out-of-band, the same way an unrelated closure GC-rooting it would,
    # without ever running this derivation's own builder for it.
    #
    # That out-of-band registration is why this half is local-store only:
    # `nix-store --register-validity` goes through ensureLocalStore() and
    # fails against a daemon store. Guarded inline rather than with
    # needLocalStore, which exits the whole file and would take test1-13
    # out of the daemon run with it.
    if [[ "$NIX_REMOTE" != "daemon" ]]; then
        clearStore
        drvPath14=$(nix-instantiate check-refs.nix -A test14)
        outPath14=$(nix-store -q --outputs "$drvPath14" | grep -v -- '-aux$')
        auxPath14=$(nix-store -q --outputs "$drvPath14" | grep -- '-aux$')
        [[ ! -e "$outPath14" ]] || fail "test14: out should not exist yet"

        mkdir -p "$outPath14"
        echo out > "$outPath14/x"
        printf '%s\n\n0\n' "$outPath14" | nix-store --register-validity
        [[ -e "$outPath14" ]] || fail "test14: out should be valid after manual registration"
        [[ ! -e "$auxPath14" ]] || fail "test14: aux should not exist yet"

        (! nix-build -o "$RESULT14aux" check-refs.nix -A test14.aux 2> "$TEST_ROOT/test14.stderr")
        grepQuiet -F \
            "is not allowed to refer to the following paths" \
            < "$TEST_ROOT/test14.stderr"
        rm -f "$RESULT14aux"
    fi
fi
