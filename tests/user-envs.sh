nixenv=$TOP/src/nix-env/nix-env
profiles="$NIX_STATE_DIR"/profiles

# Query installed: should be empty.
test "$($nixenv -p $profiles/test -q | wc -l)" -eq 0

# Query available: should contain several.
test "$($nixenv -p $profiles/test -f ./user-envs.nix -qa | wc -l)" -eq 5

# Install "foo-1.0".
$nixenv -p $profiles/test -f ./user-envs.nix -i foo-1.0

# Query installed: should contain foo-1.0 now (which should be
# executable).
test "$($nixenv -p $profiles/test -q | wc -l)" -eq 1
$nixenv -p $profiles/test -q | grep -q foo-1.0
test "$($profiles/test/bin/foo)" = "foo-1.0"

# Store the path of foo-1.0.
outPath10=$($nixenv -p $profiles/test -q --out-path --no-name | grep foo-1.0)
echo "foo-1.0 = $outPath10"
test -n "$outPath10"

# Install "foo-2.0pre1": should remove foo-1.0.
$nixenv -p $profiles/test -f ./user-envs.nix -i foo-2.0pre1

# Query installed: should contain foo-2.0pre1 now.
test "$($nixenv -p $profiles/test -q | wc -l)" -eq 1
$nixenv -p $profiles/test -q | grep -q foo-2.0pre1
test "$($profiles/test/bin/foo)" = "foo-2.0pre1"

# Upgrade "foo": should install foo-2.0.
$nixenv -p $profiles/test -f ./user-envs.nix -u foo

# Query installed: should contain foo-2.0 now.
test "$($nixenv -p $profiles/test -q | wc -l)" -eq 1
$nixenv -p $profiles/test -q | grep -q foo-2.0
test "$($profiles/test/bin/foo)" = "foo-2.0"

# Store the path of foo-2.0.
outPath20=$($nixenv -p $profiles/test -q --out-path --no-name | grep foo-2.0)
test -n "$outPath20"

# Install bar-0.1, uninstall foo.
$nixenv -p $profiles/test -f ./user-envs.nix -i bar-0.1
$nixenv -p $profiles/test -f ./user-envs.nix -e foo

# Query installed: should only contain bar-0.1 now.
if $nixenv -p $profiles/test -q | grep -q foo; then false; fi
$nixenv -p $profiles/test -q | grep -q bar

# Rollback: should bring "foo" back.
$nixenv -p $profiles/test --rollback
$nixenv -p $profiles/test -q | grep -q foo-2.0
$nixenv -p $profiles/test -q | grep -q bar

# Rollback again: should remove "bar".
$nixenv -p $profiles/test --rollback
$nixenv -p $profiles/test -q | grep -q foo-2.0
if $nixenv -p $profiles/test -q | grep -q bar; then false; fi

# Count generations.
test "$($nixenv -p $profiles/test --list-generations | wc -l)" -eq 5

# Install foo-1.0, now using its store path.
echo $outPath10
$nixenv -p $profiles/test -i "$outPath10"
$nixenv -p $profiles/test -q | grep -q foo-1.0

# Delete all old generations.
$nixenv -p $profiles/test --delete-generations old

# Run the garbage collector.  This should get rid of foo-2.0 but not
# foo-1.0.
$NIX_BIN_DIR/nix-collect-garbage
test -e "$outPath10"
if test -e "$outPath20"; then false; fi

# Uninstall everything
$nixenv -p $profiles/test -f ./user-envs.nix -e '*'
test "$($nixenv -p $profiles/test -q | wc -l)" -eq 0

# Installing "foo" should only install the newest foo.
$nixenv -p $profiles/test -f ./user-envs.nix -i foo
test "$($nixenv -p $profiles/test -q | grep foo- | wc)" -eq 1
$nixenv -p $profiles/test -q | grep -q foo-2.0

# Installing "*" should install one foo and one bar.
$nixenv -p $profiles/test -f ./user-envs.nix -e '*'
$nixenv -p $profiles/test -f ./user-envs.nix -i '*'
test "$($nixenv -p $profiles/test -q | wc)" -eq 2
$nixenv -p $profiles/test -q | grep -q foo-2.0
$nixenv -p $profiles/test -q | grep -q bar-0.1.1
