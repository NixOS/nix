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
  outputs = args: rec {
    packages.$system = rec {
      default =
        assert builtins.readFile ./message == "all good\n";
        assert builtins.readFile (args.self + "/message") == "all good\n";
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
  outputs = args:
    throw "can't evaluate self, because outputs fails to return any attribute names, but I know I can be identified as ${toString args.meta.extraAttributes.outPath}/flake.nix}";
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
  outputs = args: rec {
    packages.$system = rec {
      default =
        assert builtins.readFile ./message == "all good\n";
        assert builtins.readFile (args.self + "/message") == "all good\n";
        assert args.self.outPath == args.self.sourceInfo.outPath + "/b-low";
        assert args.meta.extraArguments.self == args.self;
        assert args.meta.extraAttributes.outPath == args.self.outPath;
        assert args.meta.sourceInfo.outPath + "/b-low" == args.self.outPath;
        assert args.meta.sourceInfo.outPath == args.self.sourceInfo.outPath;
        assert args.meta.subdir == "b-low";
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

  outputs = args: rec {
    packages =
      assert args.inp.outPath == args.inp.sourceInfo.outPath + "/b-low";
      args.inp.packages;
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
  outputs = args: rec {
    packages.$system = rec {
      default =
        assert builtins.readFile ./message == "all good\n";
        assert builtins.readFile (args.self + "/message") == "all good\n";
        assert args.self.outPath == args.self.sourceInfo.outPath;
        assert args.meta.extraArguments.self == args.self;
        assert args.meta.extraAttributes.outPath == args.self.outPath;
        assert args.meta.sourceInfo.outPath == args.self.outPath;
        assert args.meta.sourceInfo.outPath == args.self.sourceInfo.outPath;
        assert args.meta.subdir == "";
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

  outputs = args: rec {
    packages =
      assert args.inp.outPath == args.inp.sourceInfo.outPath;
      args.inp.packages;
  };
}
EOF
    nix build $clientDir --no-link

}
test_git_root_self_path
