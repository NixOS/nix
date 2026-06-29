with import ./config.nix;

let
  buildDir = "${builtins.getEnv "_NIX_TEST_BUILD_DIR"}/varlink/test-varlink";
  mkCADerivation =
    args:
    mkDerivation (
      {
        __contentAddressed = true;
        outputHashMode = "recursive";
        outputHashAlgo = "sha256";
      }
      // args
    );
  dynamic = mkCADerivation {
    name = "varlink-dynamic";
    requiredSystemFeatures = [ "builder-rpc-v1" ];
    buildCommand = ''
      ${buildDir}/varlink-dynamic ${builtins.currentSystem}
    '';
  };
  nonTrivialInner = mkCADerivation {
    name = "varlink-non-trivial-inner";
    requiredSystemFeatures = [ "builder-rpc-v1" ];
    buildCommand = ''
      ${buildDir}/varlink-non-trivial ${builtins.currentSystem} ${shell} ${path}
    '';
  };
in
{
  trivial = mkCADerivation {
    name = "varlink-trivial";
    requiredSystemFeatures = [ "builder-rpc-v1" ];
    buildCommand = ''
      mkdir out
      echo "bar" > out/foo
      ${buildDir}/varlink-trivial out
    '';
  };

  dynamicWrapper = mkCADerivation {
    name = "varlink-dynamic-wrapper";
    buildCommand = ''
      mkdir $out
      cp ${builtins.outputOf dynamic.outPath "out"} $out/bar
    '';
  };

  nonTrivialOuter = mkCADerivation {
    name = "varlink-non-trivial-outer";
    buildCommand = ''
      mkdir $out
      cp ${builtins.outputOf nonTrivialInner.outPath "out"} $out/result
    '';
  };
}
