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
      # Regression https://github.com/NixOS/nix/issues/15133
      # Tab-completing an expression that throws a non-EvalError (e.g.
      # JSONParseError from fromJSON) should not crash the REPL.
      send "err1 = builtins.fromJSON \"nixnix\"\n"
      expect "nix-repl>"
      send "err1.\t"
      sleep 0.5
      # Send Ctrl-C to cancel the current line and get a fresh prompt,
      # since tab with no completions leaves the cursor on the same line.
      send "\x03"
      expect {
        "nix-repl>" {
          puts "Got another prompt after fromJSON error."
        }
        eof {
          puts "REPL crashed after fromJSON tab-complete."
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
