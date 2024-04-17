source ./common.sh

flake1Dir=$TEST_ROOT/flake1
flake2Dir=$TEST_ROOT/flake2

mkdir -p $flake1Dir $flake2Dir

writeSimpleFlake $flake2Dir
tar cfz $TEST_ROOT/flake.tar.gz -C $TEST_ROOT flake2
hash=$(nix hash path $flake2Dir)

dep=$(nix store add-path ./common.sh)

cat > $flake1Dir/flake.nix <<EOF
{
  inputs.flake2.url = "file://$TEST_ROOT/flake.tar.gz";

  outputs = { self, flake2 }: {

    a1 = builtins.fetchTarball {
      #type = "tarball";
      url = "file://$TEST_ROOT/flake.tar.gz";
      sha256 = "$hash";
    };

    a2 = ./foo;

    a3 = ./.;

    a4 = self.outPath;

    # FIXME
    a5 = self;

    a6 = flake2.outPath;

    # FIXME
    a7 = "\${flake2}/config.nix";

    # This is only allowed in impure mode.
    a8 = builtins.storePath $dep;

    a9 = "$dep";

    drvCall = with import ./config.nix; mkDerivation {
      name = "simple";
      builder = ./simple.builder.sh;
      PATH = "";
      goodPath = path;
    };

    a10 = builtins.unsafeDiscardOutputDependency self.drvCall.drvPath;

    a11 = self.drvCall.drvPath;

    a12 = self.drvCall.outPath;

    a13 = "\${self.drvCall.drvPath}\${self.drvCall.outPath}";

    a14 = with import ./config.nix; let
      top = mkDerivation {
        name = "dot-installable";
        outputs = [ "foo" "out" ];
        meta.outputsToInstall = [ "out" ];
        buildCommand = ''
            mkdir \$foo \$out
            echo "foo" > \$foo/file
            echo "out" > \$out/file
        '';
      };
    in top // {
      foo = top.foo // {
        outputSpecified = true;
      };
    };
  };
}
EOF

cp ../simple.nix ../simple.builder.sh ../config.nix $flake1Dir/

echo bar > $flake1Dir/foo

nix build --json --out-link $TEST_ROOT/result $flake1Dir#a1
[[ -e $TEST_ROOT/result/simple.nix ]]

nix build --json --out-link $TEST_ROOT/result $flake1Dir#a2
[[ $(cat $TEST_ROOT/result) = bar ]]

nix build --json --out-link $TEST_ROOT/result $flake1Dir#a3

nix build --json --out-link $TEST_ROOT/result $flake1Dir#a4

nix build --json --out-link $TEST_ROOT/result $flake1Dir#a6
[[ -e $TEST_ROOT/result/simple.nix ]]

nix build --impure --json --out-link $TEST_ROOT/result $flake1Dir#a8
diff common.sh $TEST_ROOT/result

expectStderr 1 nix build --impure --json --out-link $TEST_ROOT/result $flake1Dir#a9 \
  | grepQuiet "has 0 entries in its context. It should only have exactly one entry"

nix build --json --out-link $TEST_ROOT/result $flake1Dir#a10
[[ $(readlink -e $TEST_ROOT/result) = *simple.drv ]]

expectStderr 1 nix build --json --out-link $TEST_ROOT/result $flake1Dir#a11 \
  | grepQuiet "has a context which refers to a complete source and binary closure"

nix build --json --out-link $TEST_ROOT/result $flake1Dir#a12
[[ -e $TEST_ROOT/result/hello ]]

expectStderr 1 nix build --impure --json --out-link $TEST_ROOT/result $flake1Dir#a13 \
  | grepQuiet "has 2 entries in its context. It should only have exactly one entry"

# Test accessing output in installables with `.` (foobarbaz.<output>)
nix build --json --no-link $flake1Dir#a14.foo | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*dot-installable.drv")) and
    (.outputs | keys == ["foo"]))
'
