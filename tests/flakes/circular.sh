# Test circular flake dependencies.
source ./common.sh

requireGit

flakeA=$TEST_ROOT/flakeA
flakeB=$TEST_ROOT/flakeB

createGitRepo $flakeA
createGitRepo $flakeB

cat > $flakeA/flake.nix <<EOF
{
  inputs.b.url = git+file://$flakeB;
  inputs.b.inputs.a.follows = "/";

  outputs = { self, b }: {
    foo = 123 + b.bar;
    xyzzy = 1000;
  };
}
EOF

git -C $flakeA add flake.nix

cat > $flakeB/flake.nix <<EOF
{
  inputs.a.url = git+file://$flakeA;

  outputs = { self, a }: {
    bar = 456 + a.xyzzy;
  };
}
EOF

git -C $flakeB add flake.nix
git -C $flakeB commit -a -m 'Foo'

[[ $(nix eval $flakeA#foo) = 1579 ]]
[[ $(nix eval $flakeA#foo) = 1579 ]]

sed -i $flakeB/flake.nix -e 's/456/789/'
git -C $flakeB commit -a -m 'Foo'

[[ $(nix eval --update-input b $flakeA#foo) = 1912 ]]

# Test list-inputs with circular dependencies
nix flake metadata $flakeA

