with import ./config.nix;

{
  slow = mkDerivation {
    name = "fixed";
    outputHashMode = "flat";
    outputHashAlgo = "sha256";
    outputHash = builtins.hashString "sha256" "fixed";

    buildCommand = ''
      echo "slow: Started" >&2
      echo slow-ready > /sync-fifo/fifo || echo "slow: No sync fifo, continuing" >&2
      echo "slow: Entering infinite loop" >&2
      while true ; do echo -n .; done
    '';
  };

  bad = mkDerivation {
    name = "fixed";
    outputHashMode = "flat";
    outputHashAlgo = "sha256";
    outputHash = builtins.hashString "sha256" "wrong";

    buildCommand = ''
      echo "bad: Waiting for sync" >&2
      cat /sync-fifo/fifo || echo "bad: No sync fifo, continuing" >&2
      echo -n fixed > $out
      echo "bad: Done" >&2
    '';
  };
}
