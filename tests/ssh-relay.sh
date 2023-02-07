source common.sh

unknownFailsOnNixOS # nix-store: error while loading shared libraries: libaws-cpp-sdk-s3.so: cannot open shared object file: Error 24
enableFeatures nix-command

echo foo > $TEST_ROOT/hello.sh

ssh_localhost=ssh://localhost
remote_store=?remote-store=$ssh_localhost

store=$ssh_localhost

store+=$remote_store
store+=$remote_store
store+=$remote_store

out=$(nix store add-path --store "$store" $TEST_ROOT/hello.sh)

[ foo = $(< $out) ]
