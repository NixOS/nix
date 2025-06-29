with import ./config.nix;

{
  # Test derivation that checks the NIX_BUILD_CORES environment variable
  testCores = mkDerivation {
    name = "test-build-cores";
    buildCommand = ''
      echo "$NIX_BUILD_CORES" > $out
    '';
  };
}
