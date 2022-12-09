source common.sh

# XXX: This shouldn’t be, but #4813 cause this test to fail
buggyNeedLocalStore "see #4813"

checkBuildTempDirRemoved ()
{
    buildDir=$(sed -n 's/CHECK_TMPDIR=//p' $1 | head -1)
    checkBuildIdFile=${buildDir}/checkBuildId
    [[ ! -f $checkBuildIdFile ]] || ! grep $checkBuildId $checkBuildIdFile
}

# written to build temp directories to verify created by this instance
checkBuildId=$(date +%s%N)

clearStore

nix-build dependencies.nix --no-out-link
nix-build dependencies.nix --no-out-link --check

# check for dangling temporary build directories
# only retain if build fails and --keep-failed is specified, or...
# ...build is non-deterministic and --check and --keep-failed are both specified
nix-build check.nix -A failed --argstr checkBuildId $checkBuildId \
    --no-out-link 2> $TEST_ROOT/log || status=$?
[ "$status" = "100" ]
checkBuildTempDirRemoved $TEST_ROOT/log

nix-build check.nix -A failed --argstr checkBuildId $checkBuildId \
    --no-out-link --keep-failed 2> $TEST_ROOT/log || status=$?
[ "$status" = "100" ]
if checkBuildTempDirRemoved $TEST_ROOT/log; then false; fi

nix-build check.nix -A deterministic --argstr checkBuildId $checkBuildId \
    --no-out-link 2> $TEST_ROOT/log
checkBuildTempDirRemoved $TEST_ROOT/log

nix-build check.nix -A deterministic --argstr checkBuildId $checkBuildId \
    --no-out-link --check --keep-failed 2> $TEST_ROOT/log
if grep -q 'may not be deterministic' $TEST_ROOT/log; then false; fi
checkBuildTempDirRemoved $TEST_ROOT/log

nix build -f check.nix deterministic --rebuild --repeat 1 \
    --argstr checkBuildId $checkBuildId --keep-failed --no-link \
    2> $TEST_ROOT/log
if grep -q 'checking is not possible' $TEST_ROOT/log; then false; fi
# Repeat is set to 1, ie. nix should build deterministic twice.
if [ "$(grep "checking outputs" $TEST_ROOT/log | wc -l)" -ne 2 ]; then false; fi
checkBuildTempDirRemoved $TEST_ROOT/log

nix-build check.nix -A nondeterministic --argstr checkBuildId $checkBuildId \
    --no-out-link 2> $TEST_ROOT/log
checkBuildTempDirRemoved $TEST_ROOT/log

nix-build check.nix -A nondeterministic --argstr checkBuildId $checkBuildId \
    --no-out-link --check 2> $TEST_ROOT/log || status=$?
grep 'may not be deterministic' $TEST_ROOT/log
[ "$status" = "104" ]
checkBuildTempDirRemoved $TEST_ROOT/log

nix build -f check.nix nondeterministic --rebuild --repeat 1 \
    --argstr checkBuildId $checkBuildId --keep-failed --no-link \
    2> $TEST_ROOT/log || status=$?
grep 'may not be deterministic' $TEST_ROOT/log
checkBuildTempDirRemoved $TEST_ROOT/log

nix-build check.nix -A nondeterministic --argstr checkBuildId $checkBuildId \
    --no-out-link --check --keep-failed 2> $TEST_ROOT/log || status=$?
grep 'may not be deterministic' $TEST_ROOT/log
[ "$status" = "104" ]
if checkBuildTempDirRemoved $TEST_ROOT/log; then false; fi

clearStore

nix-build dependencies.nix --no-out-link --repeat 3

nix-build check.nix -A nondeterministic --no-out-link --repeat 1 2> $TEST_ROOT/log || status=$?
[ "$status" = "1" ]
grep 'differs from previous round' $TEST_ROOT/log

path=$(nix-build check.nix -A fetchurl --no-out-link)

chmod +w $path
echo foo > $path
chmod -w $path

nix-build check.nix -A fetchurl --no-out-link --check
# Note: "check" doesn't repair anything, it just compares to the hash stored in the database.
[[ $(cat $path) = foo ]]

nix-build check.nix -A fetchurl --no-out-link --repair
[[ $(cat $path) != foo ]]

echo 'Hello World' > $TEST_ROOT/dummy
nix-build check.nix -A hashmismatch --no-out-link || status=$?
[ "$status" = "102" ]

echo -n > $TEST_ROOT/dummy
nix-build check.nix -A hashmismatch --no-out-link
echo 'Hello World' > $TEST_ROOT/dummy

nix-build check.nix -A hashmismatch --no-out-link --check || status=$?
[ "$status" = "102" ]

# Multiple failures with --keep-going
nix-build check.nix -A nondeterministic --no-out-link
nix-build check.nix -A nondeterministic -A hashmismatch --no-out-link --check --keep-going || status=$?
[ "$status" = "110" ]
