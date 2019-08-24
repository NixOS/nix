source common.sh

clearStore

garbage1=$(nix add-to-store --name garbage1 ./tarball.sh)
garbage2=$(nix add-to-store --name garbage2 ./tarball.sh)
garbage3=$(nix add-to-store --name garbage3 ./tarball.sh)

fake_free=$TEST_ROOT/fake-free
export _NIX_TEST_FREE_SPACE_FILE=$fake_free
echo 1100 > $fake_free

expr=$(cat <<EOF
with import ./config.nix; mkDerivation {
  name = "gc-A";
  buildCommand = ''
    [[ \$(ls \$NIX_STORE/*-garbage? | wc -l) = 3 ]]
    mkdir \$out
    echo foo > \$out/bar
    echo 1...
    sleep 2
    echo 100 > $fake_free
    echo 2...
    sleep 2
    echo 3...
    [[ \$(ls \$NIX_STORE/*-garbage? | wc -l) = 1 ]]
  '';
}
EOF
)

nix build -o $TEST_ROOT/result-A -L "($expr)" \
    --no-net --min-free 1000 --max-free 2000 --min-free-check-interval 1 &
pid=$!

expr2=$(cat <<EOF
with import ./config.nix; mkDerivation {
  name = "gc-B";
  buildCommand = ''
    mkdir \$out
    echo foo > \$out/bar
    echo 1...
    sleep 2
    echo 100 > $fake_free
    echo 2...
    sleep 2
    echo 3...
  '';
}
EOF
)

nix build -o $TEST_ROOT/result-B -L "($expr2)" \
    --no-net --min-free 1000 --max-free 2000 --min-free-check-interval 1

wait "$pid"

[[ foo = $(cat $TEST_ROOT/result-A/bar) ]]
[[ foo = $(cat $TEST_ROOT/result-B/bar) ]]
