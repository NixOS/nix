source ./common.sh

requireGit


test_subdir_self_path() {
    baseDir=$TEST_ROOT/$RANDOM
    flakeDir=$baseDir/b-low
    mkdir -p $flakeDir
    writeSimpleFlake $baseDir
    writeSimpleFlake $flakeDir

    echo all good > $flakeDir/message
    cat > $flakeDir/flake.nix <<EOF
{
  outputs = inputs: rec {
    packages.$system = rec {
      default =
        assert builtins.readFile ./message == "all good\n";
        assert builtins.readFile (inputs.self + "/message") == "all good\n";
        import ./simple.nix;
    };
  };
}
EOF
    (
        nix build $baseDir?dir=b-low --no-link
    )
}
test_subdir_self_path


test_git_subdir_self_path() {
    repoDir=$TEST_ROOT/repo-$RANDOM
    createGitRepo $repoDir
    flakeDir=$repoDir/b-low
    mkdir -p $flakeDir
    writeSimpleFlake $repoDir
    writeSimpleFlake $flakeDir

    echo all good > $flakeDir/message
    cat > $flakeDir/flake.nix <<EOF
{
  outputs = inputs: rec {
    packages.$system = rec {
      default =
        assert builtins.readFile ./message == "all good\n";
        assert builtins.readFile (inputs.self + "/message") == "all good\n";
        import ./simple.nix;
    };
  };
}
EOF
    (
        cd $flakeDir
        git add .
        git commit -m init
        # nix build
    )

    clientDir=$TEST_ROOT/client-$RANDOM
    mkdir -p $clientDir
    cat > $clientDir/flake.nix <<EOF
{
  inputs.inp = {
    type = "git";
    url = "file://$repoDir";
    dir = "b-low";
  };

  outputs = inputs: rec {
    packages = inputs.inp.packages;
  };
}
EOF
    nix build $clientDir --no-link

}
test_git_subdir_self_path
