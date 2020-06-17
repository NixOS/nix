source common.sh

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

nix-build check.nix -A nondeterministic --argstr checkBuildId $checkBuildId \
    --no-out-link 2> $TEST_ROOT/log
checkBuildTempDirRemoved $TEST_ROOT/log

nix-build check.nix -A nondeterministic --argstr checkBuildId $checkBuildId \
    --no-out-link --check 2> $TEST_ROOT/log || status=$?
grep 'may not be deterministic' $TEST_ROOT/log
[ "$status" = "104" ]
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

path=$(nix-build check.nix -A fetchurl --no-out-link --hashed-mirrors '')

chmod +w $path
echo foo > $path
chmod -w $path

nix-build check.nix -A fetchurl --no-out-link --check --hashed-mirrors ''
# Note: "check" doesn't repair anything, it just compares to the hash stored in the database.
[[ $(cat $path) = foo ]]

nix-build check.nix -A fetchurl --no-out-link --repair --hashed-mirrors ''
[[ $(cat $path) != foo ]]

nix-build check.nix -A hashmismatch --no-out-link --hashed-mirrors '' || status=$?
[ "$status" = "102" ]

echo -n > ./dummy
nix-build check.nix -A hashmismatch --no-out-link --hashed-mirrors ''
echo 'Hello World' > ./dummy

nix-build check.nix -A hashmismatch --no-out-link --check --hashed-mirrors '' || status=$?
[ "$status" = "102" ]

# Multiple failures with --keep-going
nix-build check.nix -A nondeterministic --no-out-link
nix-build check.nix -A nondeterministic -A hashmismatch --no-out-link --check --keep-going --hashed-mirrors '' || status=$?
[ "$status" = "110" ]
