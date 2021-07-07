source common.sh

if [[ -z $(type -p git) ]]; then
    echo "Git not installed; skipping flake tests"
    exit 99
fi

clearStore
rm -rf $TEST_HOME/.cache $TEST_HOME/.config

repo=$TEST_ROOT/flake

rm -rf $repo $repo.tmp
mkdir $repo
git -C $repo init
git -C $repo config user.email "foobar@example.com"
git -C $repo config user.name "Foobar"

cat > $repo/flake.nix <<EOF
{
  description = "Bla bla";

  outputs = inputs: rec {
    packages.$system.foo = import ./simple.nix;
    defaultPackage.$system = packages.$system.foo;
  };
}
EOF

git -C $repo add flake.nix
git -C $repo commit -m 'Initial'

cp ./simple.nix $repo/
nix build $repo |& grep -q "Did you forget to track it in Git" || \
    fail "Trying to access a non git-tracked file should suggest that it is probably the issue"

rm $repo/simple.nix
nix build $repo |& (! grep -q "Did you forget to track it in Git") || \
    fail "Trying to use an absent file shouldn’t suggest to git add it"
