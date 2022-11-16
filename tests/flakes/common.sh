source ../common.sh

registry=$TEST_ROOT/registry.json

requireGit() {
    if [[ -z $(type -p git) ]]; then
        echo "Git not installed; skipping flake tests"
        exit 99
    fi
}

writeSimpleFlake() {
    local flakeDir="$1"
    cat > $flakeDir/flake.nix <<EOF
{
  description = "Bla bla";

  outputs = inputs: rec {
    packages.$system = rec {
      foo = import ./simple.nix;
      default = foo;
    };

    # To test "nix flake init".
    legacyPackages.x86_64-linux.hello = import ./simple.nix;

    parent = builtins.dirOf ./.;
  };
}
EOF

    cp ../simple.nix ../simple.builder.sh ../config.nix $flakeDir/
}

createSimpleGitFlake() {
    local flakeDir="$1"
    writeSimpleFlake $flakeDir
    git -C $flakeDir add flake.nix simple.nix simple.builder.sh config.nix
    git -C $flakeDir commit -m 'Initial'
}

writeDependentFlake() {
    local flakeDir="$1"
    cat > $flakeDir/flake.nix <<EOF
{
  outputs = { self, flake1 }: {
    packages.$system.default = flake1.packages.$system.default;
    expr = assert builtins.pathExists ./flake.lock; 123;
  };
}
EOF
}

writeTrivialFlake() {
    local flakeDir="$1"
    cat > $flakeDir/flake.nix <<EOF
{
  outputs = { self }: {
    expr = 123;
  };
}
EOF
}

createGitRepo() {
    local repo="$1"
    local extraArgs="$2"

    rm -rf $repo $repo.tmp
    mkdir -p $repo

    git -C $repo init $extraArgs
    git -C $repo config user.email "foobar@example.com"
    git -C $repo config user.name "Foobar"
}
