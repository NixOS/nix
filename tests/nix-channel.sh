source common.sh

clearProfiles

rm -f $TEST_HOME/.nix-channels $TEST_HOME/.nix-profile

# Test add/list/remove.
nix-channel --add http://foo/bar xyzzy
nix-channel --list | grep -q http://foo/bar
nix-channel --remove xyzzy

[ -e $TEST_HOME/.nix-channels ]
[ "$(cat $TEST_HOME/.nix-channels)" = '' ]

# Test the XDG Base Directories support

nix-channel --add http://foo/bar xyzzy
nix-channel --list | grep -q http://foo/bar

export NIX_CONFIG="use-xdg-base-directories = true"

# The legacy channels list should be migrated
nix-channel --list 2>&1 | grep -q "Migrating"
nix-channel --list | grep -q http://foo/bar
[ -e $TEST_HOME/.local/state/nix/channels ]
[ ! -e $TEST_HOME/.nix-channels ]

# And we should be able to add new channels
nix-channel --add http://goo/doo asdfg
nix-channel --list | grep -q http://goo/doo

unset NIX_CONFIG

# And, after XDG support has been disabled, the channel list should be migrated back

nix-channel --list 2>&1 | grep -q "Migrating"
nix-channel --list | grep -q http://goo/doo
nix-channel --remove xyzzy
nix-channel --remove asdfg

[ -e $TEST_HOME/.nix-channels ]
[ ! -e $TEST_HOME/.local/state/nix/channels ]
[ "$(cat $TEST_HOME/.nix-channels)" == '' ]


# Create a channel.
rm -rf $TEST_ROOT/foo
mkdir -p $TEST_ROOT/foo
nix copy --to file://$TEST_ROOT/foo?compression="bzip2" $(nix-store -r $(nix-instantiate dependencies.nix))
rm -rf $TEST_ROOT/nixexprs
mkdir -p $TEST_ROOT/nixexprs
cp config.nix dependencies.nix dependencies.builder*.sh $TEST_ROOT/nixexprs/
ln -s dependencies.nix $TEST_ROOT/nixexprs/default.nix
(cd $TEST_ROOT && tar cvf - nixexprs) | bzip2 > $TEST_ROOT/foo/nixexprs.tar.bz2

# Test the update action.
nix-channel --add file://$TEST_ROOT/foo
nix-channel --update

# Do a query.
nix-env -qa \* --meta --xml --out-path > $TEST_ROOT/meta.xml
grep -q 'meta.*description.*Random test package' $TEST_ROOT/meta.xml
grep -q 'item.*attrPath="foo".*name="dependencies-top"' $TEST_ROOT/meta.xml

# Do an install.
nix-env -i dependencies-top
[ -e $TEST_HOME/.nix-profile/foobar ]

# Test updating from a tarball
nix-channel --add file://$TEST_ROOT/foo/nixexprs.tar.bz2 bar
nix-channel --update

# Do a query.
nix-env -qa \* --meta --xml --out-path > $TEST_ROOT/meta.xml
grep -q 'meta.*description.*Random test package' $TEST_ROOT/meta.xml
grep -q 'item.*attrPath="bar".*name="dependencies-top"' $TEST_ROOT/meta.xml
grep -q 'item.*attrPath="foo".*name="dependencies-top"' $TEST_ROOT/meta.xml

# Do an install.
nix-env -i dependencies-top
[ -e $TEST_HOME/.nix-profile/foobar ]

