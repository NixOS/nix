source common.sh

# FIXME
if [[ $(uname) != Linux ]]; then exit; fi

clearStore

rm -f $TEST_ROOT/result

export unreachable=$(nix add-to-store ./recursive.sh)

nix --experimental-features 'nix-command recursive-nix' build -o $TEST_ROOT/result -L --impure --expr '
  with import ./config.nix;
  with import <nix/config.nix>;
  mkDerivation {
    name = "recursive";
    dummy = builtins.toFile "dummy" "bla bla";
    SHELL = shell;

    # Note: this is a string without context.
    unreachable = builtins.getEnv "unreachable";

    requiredSystemFeatures = [ "recursive-nix" ];

    buildCommand = '\'\''
      mkdir $out
      PATH=${nixBinDir}:$PATH
      opts="--experimental-features nix-command"

      # Check that we can query/build paths in our input closure.
      nix $opts path-info $dummy
      nix $opts build $dummy

      # Make sure we cannot query/build paths not in out input closure.
      [[ -e $unreachable ]]
      (! nix $opts path-info $unreachable)
      (! nix $opts build $unreachable)

      # Add something to the store.
      echo foobar > foobar
      foobar=$(nix $opts add-to-store ./foobar)

      nix $opts path-info $foobar
      nix $opts build $foobar

      # Add it to our closure.
      ln -s $foobar $out/foobar

      [[ $(nix $opts path-info --all | wc -l) -eq 3 ]]

      # Build a derivation.
      nix $opts build -L --impure --expr '\''
        derivation {
          name = "inner1";
          builder = builtins.getEnv "SHELL";
          system = builtins.getEnv "system";
          fnord = builtins.toFile "fnord" "fnord";
          args = [ "-c" "echo $fnord blaat > $out" ];
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
