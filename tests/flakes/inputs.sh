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

test_extraAttributes_outPath_fail_safe() {
    baseDir=$TEST_ROOT/$RANDOM
    flakeDir=$baseDir
    mkdir -p $flakeDir
    writeSimpleFlake $baseDir

    cat > $flakeDir/flake.nix <<"EOF"
{
  outputs = inputs:
    throw "can't evaluate self, because outputs fails to return any attribute names, but I know I can be identified as ${toString inputs.meta.extraAttributes.outPath}/flake.nix}";
}
EOF
    (
        expectStderr 1 nix build $flakeDir?dir=b-low --no-link | grep -E "can't evaluate self, because outputs fails to return any attribute names, but I know I can be identified as .*/flake.nix"
    )
}
test_extraAttributes_outPath_fail_safe


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
        assert inputs.self.outPath == inputs.self.sourceInfo.outPath + "/b-low";
        assert inputs.meta.extraArguments.self == inputs.self;
        assert inputs.meta.extraAttributes.outPath == inputs.self.outPath;
        assert inputs.meta.sourceInfo.outPath + "/b-low" == inputs.self.outPath;
        assert inputs.meta.sourceInfo.outPath == inputs.self.sourceInfo.outPath;
        assert inputs.meta.subdir == "b-low";
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
    packages =
      assert inputs.inp.outPath == inputs.inp.sourceInfo.outPath + "/b-low";
      inputs.inp.packages;
  };
}
EOF
    nix build $clientDir --no-link

}
test_git_subdir_self_path


test_git_root_self_path() {
    repoDir=$TEST_ROOT/repo-$RANDOM
    createGitRepo $repoDir
    writeSimpleFlake $repoDir
    flakeDir=$repoDir

    echo all good > $flakeDir/message
    cat > $flakeDir/flake.nix <<EOF
{
  outputs = inputs: rec {
    packages.$system = rec {
      default =
        assert builtins.readFile ./message == "all good\n";
        assert builtins.readFile (inputs.self + "/message") == "all good\n";
        assert inputs.self.outPath == inputs.self.sourceInfo.outPath;
        assert inputs.meta.extraArguments.self == inputs.self;
        assert inputs.meta.extraAttributes.outPath == inputs.self.outPath;
        assert inputs.meta.sourceInfo.outPath == inputs.self.outPath;
        assert inputs.meta.sourceInfo.outPath == inputs.self.sourceInfo.outPath;
        assert inputs.meta.subdir == "";
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
  };

  outputs = inputs: rec {
    packages =
      assert inputs.inp.outPath == inputs.inp.sourceInfo.outPath;
      inputs.inp.packages;
  };
}
EOF
    nix build $clientDir --no-link

}
test_git_root_self_path
