source common.sh

clearStore
clearCache

nix run -f run.nix hello -c hello | grep 'Hello World'
nix run -f run.nix hello -c hello NixOS | grep 'Hello NixOS'

if [[ $(uname) = Linux ]]; then
    # TODO: Fix the chroot sandbox tests for Deb/RPM release builds
    if [ -e /etc/lsb-release ] || [ -e /etc/redhat-release ]; then
        exit 99
    fi

    chmod -R u+w $TEST_ROOT/store0 || true
    rm -rf $TEST_ROOT/store0

    clearStore

    path=$(nix eval --raw -f run.nix hello)

    # Note: we need the sandbox paths to ensure that the shell is
    # visible in the sandbox.
    nix run --sandbox-build-dir /build-tmp \
        --sandbox-paths '/nix? /bin? /lib? /lib64? /usr?' \
        --store $TEST_ROOT/store0 -f run.nix hello -c hello | grep 'Hello World'

    path2=$(nix run --sandbox-paths '/nix? /bin? /lib? /lib64? /usr?' --store $TEST_ROOT/store0 -f run.nix hello -c $SHELL -c 'type -p hello')

    [[ $path/bin/hello = $path2 ]]

    [[ -e $TEST_ROOT/store0/nix/store/$(basename $path)/bin/hello ]]
fi
