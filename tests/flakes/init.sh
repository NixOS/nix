source ./common.sh

requireGit

templatesDir=$TEST_ROOT/templates
flakeDir=$TEST_ROOT/flake
nixpkgsDir=$TEST_ROOT/nixpkgs

nix registry add --registry $registry templates git+file://$templatesDir
nix registry add --registry $registry nixpkgs git+file://$nixpkgsDir

createGitRepo $nixpkgsDir
createSimpleGitFlake $nixpkgsDir

# Test 'nix flake init'.
createGitRepo $templatesDir

cat > $templatesDir/flake.nix <<EOF
{
  description = "Some templates";

  outputs = { self }: {
    templates = rec {
      trivial = {
        path = ./trivial;
        description = "A trivial flake";
        welcomeText = ''
            Welcome to my trivial flake
        '';
      };
      default = trivial;
    };
  };
}
EOF

mkdir $templatesDir/trivial

cat > $templatesDir/trivial/flake.nix <<EOF
{
  description = "A flake for building Hello World";

  outputs = { self, nixpkgs }: {
    packages.x86_64-linux = rec {
      hello = nixpkgs.legacyPackages.x86_64-linux.hello;
      default = hello;
    };
  };
}
EOF
echo a > $templatesDir/trivial/a
echo b > $templatesDir/trivial/b

git -C $templatesDir add flake.nix trivial/
git -C $templatesDir commit -m 'Initial'

nix flake check templates
nix flake show templates
nix flake show templates --json | jq

createGitRepo $flakeDir
(cd $flakeDir && nix flake init)
(cd $flakeDir && nix flake init) # check idempotence
git -C $flakeDir add flake.nix
nix flake check $flakeDir
nix flake show $flakeDir
nix flake show $flakeDir --json | jq
git -C $flakeDir commit -a -m 'Initial'

# Test 'nix flake init' with benign conflicts
createGitRepo "$flakeDir"
echo a > $flakeDir/a
(cd $flakeDir && nix flake init) # check idempotence

# Test 'nix flake init' with conflicts
createGitRepo "$flakeDir"
echo b > $flakeDir/a
pushd $flakeDir
(! nix flake init) |& grep "refusing to overwrite existing file '$flakeDir/a'"
popd
git -C $flakeDir commit -a -m 'Changed'

# Test 'nix flake new'.
rm -rf $flakeDir
nix flake new -t templates#trivial $flakeDir
nix flake new -t templates#trivial $flakeDir # check idempotence
nix flake check $flakeDir
