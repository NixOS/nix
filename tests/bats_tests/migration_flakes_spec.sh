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
    cd flakes
}

@test 'flakes/flakes.sh' {
    run bash -e flakes.sh
    [ "$status" -eq 0 ]
}

@test 'flakes/run.sh' {
    run bash -e run.sh
    [ "$status" -eq 0 ]
}

@test 'flakes/mercurial.sh' {
    run bash -e mercurial.sh
    [ "$status" -eq 0 ]
}

@test 'flakes/circular.sh' {
    run bash -e circular.sh
    [ "$status" -eq 0 ]
}

@test 'flakes/init.sh' {
    run bash -e init.sh
    [ "$status" -eq 0 ]
}

@test 'flakes/follow-paths.sh' {
    run bash -e follow-paths.sh
    [ "$status" -eq 0 ]
}

@test 'flakes/bundle.sh' {
    run bash -e bundle.sh
    [ "$status" -eq 0 ]
}

@test 'flakes/check.sh' {
    run bash -e check.sh
    [ "$status" -eq 0 ]
}

@test 'flakes/search-root.sh' {
    run bash -e search-root.sh
    [ "$status" -eq 0 ]
}

@test 'flakes/config.sh' {
    run bash -e config.sh
    [ "$status" -eq 0 ]
}
