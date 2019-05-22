source common.sh

if [[ -z $(type -p git) ]]; then
    echo "Git not installed; skipping flake tests"
    exit 99
fi

clearStore

registry=$TEST_ROOT/registry.json

flake1Dir=$TEST_ROOT/flake1
flake2Dir=$TEST_ROOT/flake2
flake3Dir=$TEST_ROOT/flake3

for repo in $flake1Dir $flake2Dir $flake3Dir; do
    rm -rf $repo
    mkdir $repo
    git -C $repo init
    git -C $repo config user.email "foobar@example.com"
    git -C $repo config user.name "Foobar"
done

cat > $flake1Dir/flake.nix <<EOF
{
  name = "flake1";

  epoch = 2019;

  description = "Bla bla";

  provides = deps: rec {
    packages.foo = import ./simple.nix;
    defaultPackage = packages.foo;
  };
}
EOF

cp ./simple.nix ./simple.builder.sh ./config.nix $flake1Dir/
git -C $flake1Dir add flake.nix simple.nix simple.builder.sh config.nix
git -C $flake1Dir commit -m 'Initial'

cat > $flake2Dir/flake.nix <<EOF
{
  name = "flake2";

  epoch = 2019;

  requires = [ "flake1" ];

  description = "Fnord";

  provides = deps: rec {
    packages.bar = deps.flake1.provides.packages.foo;
  };
}
EOF

git -C $flake2Dir add flake.nix
git -C $flake2Dir commit -m 'Initial'

cat > $flake3Dir/flake.nix <<EOF
{
  name = "flake3";

  epoch = 2019;

  requires = [ "flake2" ];

  description = "Fnord";

  provides = deps: rec {
    packages.xyzzy = deps.flake2.provides.packages.bar;
  };
}
EOF

git -C $flake3Dir add flake.nix
git -C $flake3Dir commit -m 'Initial'

cat > $registry <<EOF
{
    "flakes": {
        "flake1": {
            "uri": "file://$flake1Dir"
        },
        "flake2": {
            "uri": "file://$flake2Dir"
        },
        "flake3": {
            "uri": "file://$flake3Dir"
        },
        "nixpkgs": {
            "uri": "flake1"
        }
    },
    "version": 1
}
EOF

# Test 'nix flake list'.
(( $(nix flake list --flake-registry $registry | wc -l) == 4 ))

# Test 'nix flake info'.
nix flake info --flake-registry $registry flake1 | grep -q 'ID: *flake1'

# Test 'nix flake info' on a local flake.
(cd $flake1Dir && nix flake info) | grep -q 'ID: *flake1'
(cd $flake1Dir && nix flake info .) | grep -q 'ID: *flake1'
nix flake info $flake1Dir | grep -q 'ID: *flake1'

# Test 'nix flake info --json'.
json=$(nix flake info --flake-registry $registry flake1 --json | jq .)
[[ $(echo "$json" | jq -r .description) = 'Bla bla' ]]
[[ -d $(echo "$json" | jq -r .path) ]]

# Test 'nix build' on a flake.
nix build -o $TEST_ROOT/result --flake-registry $registry flake1:foo
[[ -e $TEST_ROOT/result/hello ]]

# Test defaultPackage.
nix build -o $TEST_ROOT/result --flake-registry $registry flake1:
[[ -e $TEST_ROOT/result/hello ]]

# Building a flake with an unlocked dependency should fail in pure mode.
(! nix eval "(builtins.getFlake "$flake2Dir")")

# But should succeed in impure mode.
nix build -o $TEST_ROOT/result --flake-registry $registry flake2:bar --impure

# Test automatic lock file generation.
nix build -o $TEST_ROOT/result --flake-registry $registry $flake2Dir:bar
[[ -e $flake2Dir/flake.lock ]]
git -C $flake2Dir commit flake.lock -m 'Add flake.lock'

# Rerunning the build should not change the lockfile.
nix build -o $TEST_ROOT/result --flake-registry $registry $flake2Dir:bar
[[ -z $(git -C $flake2Dir diff master) ]]

# Now we should be able to build the flake in pure mode.
nix build -o $TEST_ROOT/result --flake-registry $registry flake2:bar

# Or without a registry.
nix build -o $TEST_ROOT/result file://$flake2Dir:bar

# Test whether indirect dependencies work.
nix build -o $TEST_ROOT/result --flake-registry $registry $flake3Dir:xyzzy

# Add dependency to flake3.
rm $flake3Dir/flake.nix

cat > $flake3Dir/flake.nix <<EOF
{
  name = "flake3";

  epoch = 2019;

  requires = [ "flake1" "flake2" ];

  description = "Fnord";

  provides = deps: rec {
    packages.xyzzy = deps.flake2.provides.packages.bar;
    packages.sth = deps.flake1.provides.packages.foo;
  };
}
EOF

git -C $flake3Dir add flake.nix
git -C $flake3Dir commit -m 'Update flake.nix'

# Check whether `nix build` works with an incomplete lockfile
nix build -o $TEST_ROOT/result --flake-registry $registry $flake3Dir:sth

# Check whether it saved the lockfile
[[ ! (-z $(git -C $flake3Dir diff master)) ]]

# Unsupported epochs should be an error.
sed -i $flake3Dir/flake.nix -e s/2019/2030/
nix build -o $TEST_ROOT/result --flake-registry $registry $flake3Dir:sth 2>&1 | grep 'unsupported epoch'
