source common.sh

if [ -z "$storeCleared" ]; then
    clearStore
fi

clearProfiles

# Query installed: should be empty.
test "$(nix-env -p $profiles/test -q '*' | wc -l)" -eq 0

mkdir -p $TEST_HOME
nix-env --switch-profile $profiles/test

# Query available: should contain several.
test "$(nix-env -f ./user-envs.nix -qa '*' | wc -l)" -eq 6
outPath10=$(nix-env -f ./user-envs.nix -qa --out-path --no-name '*' | grep foo-1.0)
drvPath10=$(nix-env -f ./user-envs.nix -qa --drv-path --no-name '*' | grep foo-1.0)
[ -n "$outPath10" -a -n "$drvPath10" ]

# Query descriptions.
nix-env -f ./user-envs.nix -qa '*' --description | grep -q silly
rm -rf $HOME/.nix-defexpr
ln -s $(pwd)/user-envs.nix $HOME/.nix-defexpr
nix-env -qa '*' --description | grep -q silly

# Query the system.
nix-env -qa '*' --system | grep -q $system

# Install "foo-1.0".
nix-env -i foo-1.0

# Query installed: should contain foo-1.0 now (which should be
# executable).
test "$(nix-env -q '*' | wc -l)" -eq 1
nix-env -q '*' | grep -q foo-1.0
test "$($profiles/test/bin/foo)" = "foo-1.0"

# Test nix-env -qc to compare installed against available packages, and vice versa.
nix-env -qc '*' | grep -q '< 2.0'
nix-env -qac '*' | grep -q '> 1.0'

# Test the -b flag to filter out source-only packages.
[ "$(nix-env -qab | wc -l)" -eq 1 ]

# Test the -s flag to get package status.
nix-env -qas | grep -q 'IP-  foo-1.0'
nix-env -qas | grep -q -- '---  bar-0.1'

# Disable foo.
nix-env --set-flag active false foo
(! [ -e "$profiles/test/bin/foo" ])

# Enable foo.
nix-env --set-flag active true foo
[ -e "$profiles/test/bin/foo" ]

# Store the path of foo-1.0.
outPath10_=$(nix-env -q --out-path --no-name '*' | grep foo-1.0)
echo "foo-1.0 = $outPath10"
[ "$outPath10" = "$outPath10_" ]

# Install "foo-2.0pre1": should remove foo-1.0.
nix-env -i foo-2.0pre1

# Query installed: should contain foo-2.0pre1 now.
test "$(nix-env -q '*' | wc -l)" -eq 1
nix-env -q '*' | grep -q foo-2.0pre1
test "$($profiles/test/bin/foo)" = "foo-2.0pre1"

# Upgrade "foo": should install foo-2.0.
NIX_PATH=nixpkgs=./user-envs.nix:$NIX_PATH nix-env -f '<nixpkgs>' -u foo

# Query installed: should contain foo-2.0 now.
test "$(nix-env -q '*' | wc -l)" -eq 1
nix-env -q '*' | grep -q foo-2.0
test "$($profiles/test/bin/foo)" = "foo-2.0"

# Store the path of foo-2.0.
outPath20=$(nix-env -q --out-path --no-name '*' | grep foo-2.0)
test -n "$outPath20"

# Install bar-0.1, uninstall foo.
nix-env -i bar-0.1
nix-env -e foo

# Query installed: should only contain bar-0.1 now.
if nix-env -q '*' | grep -q foo; then false; fi
nix-env -q '*' | grep -q bar

# Rollback: should bring "foo" back.
oldGen="$(nix-store -q --resolve $profiles/test)"
nix-env --rollback
[ "$(nix-store -q --resolve $profiles/test)" != "$oldGen" ]
nix-env -q '*' | grep -q foo-2.0
nix-env -q '*' | grep -q bar

# Rollback again: should remove "bar".
nix-env --rollback
nix-env -q '*' | grep -q foo-2.0
if nix-env -q '*' | grep -q bar; then false; fi

# Count generations.
nix-env --list-generations
test "$(nix-env --list-generations | wc -l)" -eq 7

# Doing the same operation twice results in the same generation, which triggers
# "lazy" behaviour and does not create a new symlink.

nix-env -i foo
nix-env -i foo

# Count generations.
nix-env --list-generations
test "$(nix-env --list-generations | wc -l)" -eq 8

# Switch to a specified generation.
nix-env --switch-generation 7
[ "$(nix-store -q --resolve $profiles/test)" = "$oldGen" ]

# Install foo-1.0, now using its store path.
nix-env -i "$outPath10"
nix-env -q '*' | grep -q foo-1.0
nix-store -qR $profiles/test | grep "$outPath10"
nix-store -q --referrers-closure $profiles/test | grep "$(nix-store -q --resolve $profiles/test)"
[ "$(nix-store -q --deriver "$outPath10")" = $drvPath10 ]

# Uninstall foo-1.0, using a symlink to its store path.
ln -sfn $outPath10/bin/foo $TEST_ROOT/symlink
nix-env -e $TEST_ROOT/symlink
if nix-env -q '*' | grep -q foo; then false; fi
(! nix-store -qR $profiles/test | grep "$outPath10")

# Install foo-1.0, now using a symlink to its store path.
nix-env -i $TEST_ROOT/symlink
nix-env -q '*' | grep -q foo

# Delete all old generations.
nix-env --delete-generations old

# Run the garbage collector.  This should get rid of foo-2.0 but not
# foo-1.0.
nix-collect-garbage
test -e "$outPath10"
(! [ -e "$outPath20" ])

# Uninstall everything
nix-env -e '*'
test "$(nix-env -q '*' | wc -l)" -eq 0

# Installing "foo" should only install the newest foo.
nix-env -i foo
test "$(nix-env -q '*' | grep foo- | wc -l)" -eq 1
nix-env -q '*' | grep -q foo-2.0

# On the other hand, this should install both (and should fail due to
# a collision).
nix-env -e '*'
(! nix-env -i foo-1.0 foo-2.0)

# Installing "*" should install one foo and one bar.
nix-env -e '*'
nix-env -i '*'
test "$(nix-env -q '*' | wc -l)" -eq 2
nix-env -q '*' | grep -q foo-2.0
nix-env -q '*' | grep -q bar-0.1.1

# Test priorities: foo-0.1 has a lower priority than foo-1.0, so it
# should be possible to install both without a collision.  Also test
# ‘--set-flag priority’ to manually override the declared priorities.
nix-env -e '*'
nix-env -i foo-0.1 foo-1.0
[ "$($profiles/test/bin/foo)" = "foo-1.0" ]
nix-env --set-flag priority 1 foo-0.1
[ "$($profiles/test/bin/foo)" = "foo-0.1" ]

# Test nix-env --set.
nix-env --set $outPath10
[ "$(nix-store -q --resolve $profiles/test)" = $outPath10 ]
nix-env --set $drvPath10
[ "$(nix-store -q --resolve $profiles/test)" = $outPath10 ]
