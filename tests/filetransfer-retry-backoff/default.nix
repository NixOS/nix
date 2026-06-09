# Integration test for file-transfer retry backoff behaviour.
{
  runCommand,
  python3,
  nix,
  writeText,
}:

let
  testScript = ./test_retry_backoff.py;

  nixConf = writeText "nix.conf" ''
    experimental-features = nix-command
    filetransfer-retry-jitter = false
    substituters =
  '';
in

runCommand "filetransfer-retry-backoff"
  {
    nativeBuildInputs = [
      nix
      python3
    ];
    # macOS sandbox blocks network by default; this allows localhost access
    __darwinAllowLocalNetworking = true;
  }
  ''
    # nix-prefetch-url needs a minimal nix environment
    export NIX_STATE_DIR=$TMPDIR/nix-state
    export NIX_LOG_DIR=$TMPDIR/nix-log
    export NIX_STORE_DIR=$TMPDIR/nix-store
    export NIX_CONF_DIR=$TMPDIR/nix-conf
    mkdir -p "$NIX_STATE_DIR" "$NIX_LOG_DIR" "$NIX_STORE_DIR" "$NIX_CONF_DIR"
    cp ${nixConf} "$NIX_CONF_DIR/nix.conf"

    python3 ${testScript}
    mkdir -p $out
  ''
