#!/usr/bin/env bash

source common.sh

clearProfiles

rm -f "$TEST_HOME"/.nix-channels "$TEST_HOME"/.nix-profile

# Test add/list/remove.
nix-channel --add http://foo/bar xyzzy
nix-channel --list | grepQuiet http://foo/bar
nix-channel --remove xyzzy
[[ $(nix-channel --list-generations | wc -l) == 1 ]]

[ -e "$TEST_HOME"/.nix-channels ]
[ "$(cat "$TEST_HOME"/.nix-channels)" = '' ]

# Test the XDG Base Directories support

export NIX_CONFIG="use-xdg-base-directories = true"

nix-channel --add http://foo/bar xyzzy
nix-channel --list | grepQuiet http://foo/bar
nix-channel --remove xyzzy

unset NIX_CONFIG

[ -e "$TEST_HOME"/.local/state/nix/channels ]
[ "$(cat "$TEST_HOME"/.local/state/nix/channels)" = '' ]

# Create a channel.
rm -rf "$TEST_ROOT"/foo
mkdir -p "$TEST_ROOT"/foo
drvPath=$(nix-instantiate dependencies.nix)
nix copy --to file://"$TEST_ROOT"/foo?compression="bzip2" "$(nix-store -r "$drvPath")"
rm -rf "$TEST_ROOT"/nixexprs
mkdir -p "$TEST_ROOT"/nixexprs
cp "${config_nix}" dependencies.nix dependencies.builder*.sh "$TEST_ROOT"/nixexprs/
ln -s dependencies.nix "$TEST_ROOT"/nixexprs/default.nix
(cd "$TEST_ROOT" && tar cvf - nixexprs) | bzip2 > "$TEST_ROOT"/foo/nixexprs.tar.bz2

# Test the update action.
nix-channel --add file://"$TEST_ROOT"/foo
nix-channel --update
[[ $(nix-channel --list-generations | wc -l) == 2 ]]

# Do a query.
nix-env -qa \* --meta --xml --out-path > "$TEST_ROOT"/meta.xml
grepQuiet 'meta.*description.*Random test package' "$TEST_ROOT"/meta.xml
grepQuiet 'item.*attrPath="foo".*name="dependencies-top"' "$TEST_ROOT"/meta.xml

# Do an install.
nix-env -i dependencies-top
[ -e "$TEST_HOME"/.nix-profile/foobar ]

# Test updating from a tarball
nix-channel --add file://"$TEST_ROOT"/foo/nixexprs.tar.bz2 bar
nix-channel --update

# Do a query.
nix-env -qa \* --meta --xml --out-path > "$TEST_ROOT"/meta.xml
grepQuiet 'meta.*description.*Random test package' "$TEST_ROOT"/meta.xml
grepQuiet 'item.*attrPath="bar".*name="dependencies-top"' "$TEST_ROOT"/meta.xml
grepQuiet 'item.*attrPath="foo".*name="dependencies-top"' "$TEST_ROOT"/meta.xml

# Do an install.
nix-env -i dependencies-top
[ -e "$TEST_HOME"/.nix-profile/foobar ]

# Test evaluation through a channel symlink (#9882).
drvPath=$(nix-instantiate '<foo/dependencies.nix>')

# Add a test for the special case behaviour of 'nixpkgs' in the
# channels for root (see EvalSettings::getDefaultNixPath()).
if ! isTestOnNixOS; then
    nix-channel --add file://"$TEST_ROOT"/foo nixpkgs
    nix-channel --update
    mv "$TEST_HOME"/.local/state/nix/profiles "$TEST_ROOT"/var/nix/profiles/per-user/root
    drvPath2=$(nix-instantiate '<nixpkgs>')
    [[ "$drvPath" = "$drvPath2" ]]
fi
