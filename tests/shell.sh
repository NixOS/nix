source common.sh

clearStore
clearCache

nix shell -f shell-hello.nix hello -c hello | grep 'Hello World'
nix shell -f shell-hello.nix hello -c hello NixOS | grep 'Hello NixOS'

if ! canUseSandbox; then exit 99; fi

chmod -R u+w $TEST_ROOT/store0 || true
rm -rf $TEST_ROOT/store0

clearStore

path=$(nix eval --raw -f shell-hello.nix hello)

# Note: we need the sandbox paths to ensure that the shell is
# visible in the sandbox.
nix shell --sandbox-build-dir /build-tmp \
    --sandbox-paths '/nix? /bin? /lib? /lib64? /usr?' \
    --store $TEST_ROOT/store0 -f shell-hello.nix hello -c hello | grep 'Hello World'

path2=$(nix shell --sandbox-paths '/nix? /bin? /lib? /lib64? /usr?' --store $TEST_ROOT/store0 -f shell-hello.nix hello -c $SHELL -c 'type -p hello')

[[ $path/bin/hello = $path2 ]]

[[ -e $TEST_ROOT/store0/nix/store/$(basename $path)/bin/hello ]]
