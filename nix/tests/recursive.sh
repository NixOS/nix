source common.sh

sed -i 's/experimental-features .*/& recursive-nix/' "$NIX_CONF_DIR"/nix.conf
restartDaemon

# FIXME
if [[ $(uname) != Linux ]]; then exit 99; fi

clearStore

rm -f $TEST_ROOT/result

export unreachable=$(nix store add-path ./recursive.sh)

NIX_BIN_DIR=$(dirname $(type -p nix)) nix --extra-experimental-features 'nix-command recursive-nix' build -o $TEST_ROOT/result -L --impure --expr '
  with import ./config.nix;
  mkDerivation rec {
    name = "recursive";
    dummy = builtins.toFile "dummy" "bla bla";
    SHELL = shell;

    # Note: this is a string without context.
    unreachable = builtins.getEnv "unreachable";

    NIX_TESTS_CA_BY_DEFAULT = builtins.getEnv "NIX_TESTS_CA_BY_DEFAULT";

    requiredSystemFeatures = [ "recursive-nix" ];

    buildCommand = '\'\''
      mkdir $out
      opts="--experimental-features nix-command ${if (NIX_TESTS_CA_BY_DEFAULT == "1") then "--extra-experimental-features ca-derivations" else ""}"

      PATH=${builtins.getEnv "NIX_BIN_DIR"}:$PATH

      # Check that we can query/build paths in our input closure.
      nix $opts path-info $dummy
      nix $opts build $dummy

      # Make sure we cannot query/build paths not in out input closure.
      [[ -e $unreachable ]]
      (! nix $opts path-info $unreachable)
      (! nix $opts build $unreachable)

      # Add something to the store.
      echo foobar > foobar
      foobar=$(nix $opts store add-path ./foobar)

      nix $opts path-info $foobar
      nix $opts build $foobar

      # Add it to our closure.
      ln -s $foobar $out/foobar

      [[ $(nix $opts path-info --all | wc -l) -eq 4 ]]

      # Build a derivation.
      nix $opts build -L --impure --expr '\''
        with import ${./config.nix};
        mkDerivation {
          name = "inner1";
          buildCommand = "echo $fnord blaat > $out";
          fnord = builtins.toFile "fnord" "fnord";
        }
      '\''

      [[ $(nix $opts path-info --json ./result) =~ fnord ]]

      ln -s $(nix $opts path-info ./result) $out/inner1
    '\'\'';
  }
'

[[ $(cat $TEST_ROOT/result/inner1) =~ blaat ]]

# Make sure the recursively created paths are in the closure.
nix path-info -r $TEST_ROOT/result | grep foobar
nix path-info -r $TEST_ROOT/result | grep fnord
nix path-info -r $TEST_ROOT/result | grep inner1
