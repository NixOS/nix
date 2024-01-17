source common.sh

echo foo > $TEST_ROOT/hello.sh

ssh_localhost=ssh://localhost
remote_store=?remote-store=$ssh_localhost

store=$ssh_localhost

store+=$remote_store
store+=$remote_store
store+=$remote_store

out=$(nix store add-path --store "$store" $TEST_ROOT/hello.sh)

[ foo = $(< $out) ]
