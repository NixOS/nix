source common.sh

needLocalStore "“min-free” and “max-free” are daemon options"

clearStore

garbage1=$(nix store add-path --name garbage1 ./nar-access.sh)
garbage2=$(nix store add-path --name garbage2 ./nar-access.sh)
garbage3=$(nix store add-path --name garbage3 ./nar-access.sh)

ls -l $garbage3
POSIXLY_CORRECT=1 du $garbage3

fake_free=$TEST_ROOT/fake-free
export _NIX_TEST_FREE_SPACE_FILE=$fake_free
echo 1100 > $fake_free

fifoLock=$TEST_ROOT/fifoLock
mkfifo "$fifoLock"

expr=$(cat <<EOF
with import ./config.nix; mkDerivation {
  name = "gc-A";
  buildCommand = ''
    set -x
    [[ \$(ls \$NIX_STORE/*-garbage? | wc -l) = 3 ]]

    mkdir \$out
    echo foo > \$out/bar

    # Pretend that we run out of space
    echo 100 > ${fake_free}.tmp1
    mv ${fake_free}.tmp1 $fake_free

    # Wait for the GC to run
    for i in {1..20}; do
        echo ''\${i}...
        if [[ \$(ls \$NIX_STORE/*-garbage? | wc -l) = 1 ]]; then
            exit 0
        fi
        sleep 1
    done
    exit 1
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

    # Wait for the first build to finish
    cat "$fifoLock"
  '';
}
EOF
)

nix build --impure -v -o $TEST_ROOT/result-A -L --expr "$expr" \
    --min-free 1000 --max-free 2000 --min-free-check-interval 1 &
pid1=$!

nix build --impure -v -o $TEST_ROOT/result-B -L --expr "$expr2" \
    --min-free 1000 --max-free 2000 --min-free-check-interval 1 &
pid2=$!

# Once the first build is done, unblock the second one.
# If the first build fails, we need to postpone the failure to still allow
# the second one to finish
wait "$pid1" || FIRSTBUILDSTATUS=$?
echo "unlock" > $fifoLock
( exit ${FIRSTBUILDSTATUS:-0} )
wait "$pid2"

[[ foo = $(cat $TEST_ROOT/result-A/bar) ]]
[[ foo = $(cat $TEST_ROOT/result-B/bar) ]]
