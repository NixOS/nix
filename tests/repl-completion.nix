{
  runCommand,
  nix,
  expect,
}:

# We only use expect when necessary, e.g. for testing tab completion in nix repl.
# See also tests/functional/repl.sh

runCommand "repl-completion"
  {
    nativeBuildInputs = [
      expect
      nix
    ];
    expectScript = ''
      # Regression https://github.com/NixOS/nix/pull/10778
      spawn nix repl --offline --extra-experimental-features nix-command
      expect "nix-repl>"
      send "foo = import ./does-not-exist.nix\n"
      expect "nix-repl>"
      send "foo.\t"
      expect {
        "nix-repl>" {
          puts "Got another prompt. Good."
        }
        eof {
          puts "Got EOF. Bad."
          exit 1
        }
      }
      exit 0
    '';
    passAsFile = [ "expectScript" ];
  }
  ''
    export NIX_STORE=$TMPDIR/store
    export NIX_STATE_DIR=$TMPDIR/state
    export HOME=$TMPDIR/home
    mkdir $HOME

    nix-store --init
    expect $expectScriptPath
    touch $out
  ''
