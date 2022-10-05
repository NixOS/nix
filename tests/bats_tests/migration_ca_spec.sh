setup() {
    bats_load_library bats-support
    bats_load_library bats-assert
    bats_require_minimum_version 1.5.0

    export NIX_REMOTE=""
    export TEST_ROOT=$(mktemp -d /tmp/nix-test.XXXXXXXXXXXXXXXXXXXXXXXXX)
    . ./common.sh
    bash init.sh
    clearStore
    clearCache
    cd ca
}

@test 'ca/gc.sh' {
    run bash -e gc.sh
    [ "$status" -eq 0 ]
}

@test 'ca/build.sh' {
    run bash -e build.sh
    [ "$status" -eq 0 ]
}

@test 'ca/concurrent-builds.sh' {
    run bash -e concurrent-builds.sh
    [ "$status" -eq 0 ]
}

@test 'ca/build-with-garbage-path.sh' {
    run bash -e build-with-garbage-path.sh
    [ "$status" -eq 0 ]
}

@test 'ca/substitute.sh' {
    run bash -e substitute.sh
    [ "$status" -eq 0 ]
}

@test 'ca/signatures.sh' {
    run bash -e signatures.sh
    [ "$status" -eq 0 ]
}

@test 'ca/nix-shell.sh' {
    run bash -e nix-shell.sh
    [ "$status" -eq 0 ]
}

@test 'ca/nix-copy.sh' {
    run bash -e nix-copy.sh
    [ "$status" -eq 0 ]
}

@test 'ca/duplicate-realisation-in-closure.sh' {
    run bash -e duplicate-realisation-in-closure.sh
    [ "$status" -eq 0 ]
}

@test 'ca/post-hook.sh' {
    run bash -e post-hook.sh
    [ "$status" -eq 0 ]
}

@test 'ca/repl.sh' {
    run bash -e repl.sh
    [ "$status" -eq 0 ]
}

@test 'ca/recursive.sh' {
    run bash -e recursive.sh
    [ "$status" -eq 0 ]
}

@test 'ca/import-derivation.sh' {
    run bash -e import-derivation.sh
    [ "$status" -eq 0 ]
}

@test 'ca/nix-run.sh' {
    run bash -e nix-run.sh
    [ "$status" -eq 0 ]
}

@test 'ca/selfref-gc.sh' {
    run bash -e selfref-gc.sh
    [ "$status" -eq 0 ]
}
