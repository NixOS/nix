source common.sh

if [[ -z $(type -p git) ]]; then
    echo "Git not installed; skipping flake tests"
    exit 99
fi

clearStore

registry=$TEST_ROOT/registry.json

flake1=$TEST_ROOT/flake1
flake2=$TEST_ROOT/flake2
flake3=$TEST_ROOT/flake3

for repo in $flake1 $flake2 $flake3; do
    rm -rf $repo
    mkdir $repo
    git -C $repo init
    git -C $repo config user.email "foobar@example.com"
    git -C $repo config user.name "Foobar"
done

cat > $flake1/flake.nix <<EOF
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

cp ./simple.nix ./simple.builder.sh ./config.nix $flake1/
git -C $flake1 add flake.nix simple.nix simple.builder.sh config.nix
git -C $flake1 commit -m 'Initial'

cat > $flake2/flake.nix <<EOF
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

git -C $flake2 add flake.nix
git -C $flake2 commit -m 'Initial'

cat > $flake3/flake.nix <<EOF
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

git -C $flake3 add flake.nix
git -C $flake3 commit -m 'Initial'

cat > $registry <<EOF
{
    "flakes": {
        "flake1": {
            "uri": "file://$flake1"
        },
        "flake2": {
            "uri": "file://$flake2"
        },
        "flake3": {
            "uri": "file://$flake3"
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
(! nix build -o $TEST_ROOT/result --flake-registry $registry flake2:bar)

# But should succeed in impure mode.
# FIXME: this currently fails.
#nix build -o $TEST_ROOT/result --flake-registry $registry flake2:bar --impure

# Test automatic lock file generation.
nix build -o $TEST_ROOT/result --flake-registry $registry $flake2:bar
[[ -e $flake2/flake.lock ]]
git -C $flake2 commit flake.lock -m 'Add flake.lock'

# Rerunning the build should not change the lockfile.
nix build -o $TEST_ROOT/result --flake-registry $registry $flake2:bar
[[ -z $(git -C $flake2 diff) ]]

# Now we should be able to build the flake in pure mode.
nix build -o $TEST_ROOT/result --flake-registry $registry flake2:bar

# Or without a registry.
nix build -o $TEST_ROOT/result file://$flake2:bar

# Test whether indirect dependencies work.
nix build -o $TEST_ROOT/result --flake-registry $registry $flake3:xyzzy
