with import ./config.nix;

let
  innerExpr = ''
    let
      dep = derivation {
        name = "dep";
        system = "${system}";
        builder = "${shell}";
        PATH = "${path}";
        args = [ "-c" "echo dep > $out" ];
      };
    in
    derivation {
      name = "main";
      system = "${system}";
      builder = "${shell}";
      PATH = "${path}";
      args = [ "-c" "cat ''${dep} > $out; echo main >> $out" ];
    }
  '';

  planner = mkDerivation {
    name = "main.drv";

    requiredSystemFeatures = [ "builder-rpc-v0" ];

    inherit innerExpr;

    buildCommand = ''
      set -e
      set -u

      PATH=${builtins.getEnv "NIX_BIN_DIR"}:$PATH
      export NIX_CONFIG='extra-experimental-features = nix-command ca-derivations dynamic-derivations'
      export HOME="$TMPDIR"

      drvPath=$(nix-instantiate --store "$NIX_REMOTE" --expr "$innerExpr")
      nix store submit-output "$drvPath" out
    '';

    __contentAddressed = true;
    outputHashMode = "text";
    outputHashAlgo = "sha256";
  };
in
{
  result = builtins.outputOf planner.outPath "out";
}
