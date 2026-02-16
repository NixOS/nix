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
    passAsFile = [
      "expectScript"
    ];
  }
  ''
    export NIX_STORE=$TMPDIR/store
    export NIX_STATE_DIR=$TMPDIR/state
    export HOME=$TMPDIR/home
    mkdir $HOME

    nix-store --init
    expect $expectScriptPath

    # Write a 300-char line to the history file, then run a REPL session
    # that reads it back (read_history) and writes it out (write_history).
    histFile=$HOME/.local/share/nix/repl-history
    mkdir -p "$(dirname "$histFile")"
    printf '%0300d\n' 0 | tr '0' 'a' > "$histFile"
    echo "short" >> "$histFile"

    # unbuffer allocates a pty so nix repl runs the interactive
    # ReadlineLikeInteracter path (read_history on init, write_history
    # on exit). Plain piped input skips history entirely.
    echo ":q" | unbuffer -p nix repl --offline --extra-experimental-features nix-command 2>/dev/null || true

    # Verify the long line survived the read/write cycle.
    maxLen=$(awk '{ print length }' "$histFile" | sort -rn | head -1)
    if [ "$maxLen" -lt 300 ]; then
      echo "FAIL: long history line was truncated (max length: $maxLen)"
      exit 1
    fi
    echo "Long history line preserved (length: $maxLen)."

    touch $out
  ''
