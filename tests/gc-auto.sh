source common.sh

clearStore

garbage1=$(nix add-to-store --name garbage1 ./nar-access.sh)
garbage2=$(nix add-to-store --name garbage2 ./nar-access.sh)
garbage3=$(nix add-to-store --name garbage3 ./nar-access.sh)

ls -l $garbage3
POSIXLY_CORRECT=1 du $garbage3

fake_free=$TEST_ROOT/fake-free
export _NIX_TEST_FREE_SPACE_FILE=$fake_free
echo 1100 > $fake_free

expr=$(cat <<EOF
with import ./config.nix; mkDerivation {
  name = "gc-A";
  buildCommand = ''
    set -x
    [[ \$(ls \$NIX_STORE/*-garbage? | wc -l) = 3 ]]
    mkdir \$out
    echo foo > \$out/bar
    echo 1...
    sleep 2
    echo 200 > ${fake_free}.tmp1
    mv ${fake_free}.tmp1 $fake_free
    echo 2...
    sleep 2
    echo 3...
    sleep 2
    echo 4...
    [[ \$(ls \$NIX_STORE/*-garbage? | wc -l) = 1 ]]
  '';
}
EOF
)

expr2=$(cat <<EOF
with import ./config.nix; mkDerivation {
  name = "gc-B";
  buildCommand = ''
    set -x
    mkdir \$out
    echo foo > \$out/bar
    echo 1...
    sleep 2
    echo 200 > ${fake_free}.tmp2
    mv ${fake_free}.tmp2 $fake_free
    echo 2...
    sleep 2
    echo 3...
    sleep 2
    echo 4...
  '';
}
EOF
)

nix build -v -o $TEST_ROOT/result-A -L "($expr)" \
    --min-free 1000 --max-free 2000 --min-free-check-interval 1 &
pid=$!

nix build -v -o $TEST_ROOT/result-B -L "($expr2)" \
    --min-free 1000 --max-free 2000 --min-free-check-interval 1

wait "$pid"

[[ foo = $(cat $TEST_ROOT/result-A/bar) ]]
[[ foo = $(cat $TEST_ROOT/result-B/bar) ]]
