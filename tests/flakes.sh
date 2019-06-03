source common.sh

if [[ -z $(type -p git) ]]; then
    echo "Git not installed; skipping flake tests"
    exit 99
fi

export _NIX_FORCE_HTTP=1

clearStore
rm -rf $TEST_HOME/.cache

registry=$TEST_ROOT/registry.json

flake1Dir=$TEST_ROOT/flake1
flake2Dir=$TEST_ROOT/flake2
flake3Dir=$TEST_ROOT/flake3
nonFlakeDir=$TEST_ROOT/nonFlake

for repo in $flake1Dir $flake2Dir $flake3Dir $nonFlakeDir; do
    rm -rf $repo $repo.tmp
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

  outputs = inputs: rec {
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

  inputs = [ "flake1" ];

  description = "Fnord";

  outputs = inputs: rec {
    packages.bar = inputs.flake1.outputs.packages.foo;
  };
}
EOF

git -C $flake2Dir add flake.nix
git -C $flake2Dir commit -m 'Initial'

cat > $flake3Dir/flake.nix <<EOF
{
  name = "flake3";

  epoch = 2019;

  inputs = [ "flake2" ];

  description = "Fnord";

  outputs = inputs: rec {
    packages.xyzzy = inputs.flake2.outputs.packages.bar;
  };
}
EOF

git -C $flake3Dir add flake.nix
git -C $flake3Dir commit -m 'Initial'

cat > $nonFlakeDir/README.md <<EOF
Not much
EOF

git -C $nonFlakeDir add README.md
git -C $nonFlakeDir commit -m 'Initial'

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
(cd $flake1Dir && nix flake info --flake-registry $registry) | grep -q 'ID: *flake1'
(cd $flake1Dir && nix flake info --flake-registry $registry .) | grep -q 'ID: *flake1'
nix flake info --flake-registry $registry $flake1Dir | grep -q 'ID: *flake1'

# Test 'nix flake info --json'.
json=$(nix flake info --flake-registry $registry flake1 --json | jq .)
[[ $(echo "$json" | jq -r .description) = 'Bla bla' ]]
[[ -d $(echo "$json" | jq -r .path) ]]
[[ $(echo "$json" | jq -r .lastModified) = $(git -C $flake1Dir log -n1 --format=%ct) ]]

# Test 'nix build' on a flake.
nix build -o $TEST_ROOT/result --flake-registry $registry flake1:foo
[[ -e $TEST_ROOT/result/hello ]]

# Test defaultPackage.
nix build -o $TEST_ROOT/result --flake-registry $registry flake1
[[ -e $TEST_ROOT/result/hello ]]

nix build -o $TEST_ROOT/result --flake-registry $registry $flake1Dir
nix build -o $TEST_ROOT/result --flake-registry $registry file://$flake1Dir

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
# FIXME: shouldn't need '--flake-registry /no-registry'?
nix build -o $TEST_ROOT/result --flake-registry /no-registry file://$flake2Dir:bar --tarball-ttl 0

# Test whether indirect dependencies work.
nix build -o $TEST_ROOT/result --flake-registry $registry $flake3Dir:xyzzy

# Add dependency to flake3.
rm $flake3Dir/flake.nix

cat > $flake3Dir/flake.nix <<EOF
{
  name = "flake3";

  epoch = 2019;

  inputs = [ "flake1" "flake2" ];

  description = "Fnord";

  outputs = inputs: rec {
    packages.xyzzy = inputs.flake2.outputs.packages.bar;
    packages.sth = inputs.flake1.outputs.packages.foo;
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

# Test whether registry caching works.
nix flake list --flake-registry file://$registry | grep -q flake3
mv $registry $registry.tmp
nix flake list --flake-registry file://$registry --tarball-ttl 0 | grep -q flake3
mv $registry.tmp $registry

# Test whether flakes are registered as GC roots for offline use.
rm -rf $TEST_HOME/.cache
nix build -o $TEST_ROOT/result --flake-registry file://$registry file://$flake2Dir:bar
mv $flake1Dir $flake1Dir.tmp
mv $flake2Dir $flake2Dir.tmp
nix-store --gc
nix build -o $TEST_ROOT/result --flake-registry file://$registry file://$flake2Dir:bar
nix build -o $TEST_ROOT/result --flake-registry file://$registry file://$flake2Dir:bar --tarball-ttl 0
mv $flake1Dir.tmp $flake1Dir
mv $flake2Dir.tmp $flake2Dir

# Add nonFlakeInputs to flake3.
rm $flake3Dir/flake.nix

cat > $flake3Dir/flake.nix <<EOF
{
  name = "flake3";

  epoch = 2019;

  inputs = [ "flake1" "flake2" ];

  nonFlakeInputs = {
    nonFlake = "$nonFlakeDir";
  };

  description = "Fnord";

  outputs = inputs: rec {
    packages.xyzzy = inputs.flake2.outputs.packages.bar;
    packages.sth = inputs.flake1.outputs.packages.foo;
  };
}
EOF

git -C $flake3Dir add flake.nix
git -C $flake3Dir commit -m 'Add nonFlakeInputs'

# Check whether `nix build` works with a lockfile which is missing a nonFlakeInputs
nix build -o $TEST_ROOT/result --flake-registry $registry $flake3Dir:sth
