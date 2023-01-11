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
    a7 = "${flake2}/config.nix";

    # This is only allowed in impure mode.
    a8 = builtins.storePath $dep;

    a9 = "$dep";
  };
}
EOF

echo bar > $flake1Dir/foo

nix build --json --out-link $TEST_ROOT/result $flake1Dir#a1
[[ -e $TEST_ROOT/result/simple.nix ]]

nix build --json --out-link $TEST_ROOT/result $flake1Dir#a2
[[ $(cat $TEST_ROOT/result) = bar ]]

nix build --json --out-link $TEST_ROOT/result $flake1Dir#a3

nix build --json --out-link $TEST_ROOT/result $flake1Dir#a4

# Add an uncopyable file to test laziness.
mkfifo $flake1Dir/fifo
(! nix build --json --out-link $TEST_ROOT/result $flake1Dir#a3)

nix build --json --out-link $TEST_ROOT/result $flake1Dir#a6
[[ -e $TEST_ROOT/result/simple.nix ]]

nix build --impure --json --out-link $TEST_ROOT/result $flake1Dir#a8
diff common.sh $TEST_ROOT/result

(! nix build --impure --json --out-link $TEST_ROOT/result $flake1Dir#a9)
