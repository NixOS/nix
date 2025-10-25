{
  nixFlake ? builtins.getFlake ("git+file://" + toString ../../..),
  system ? builtins.currentSystem,
  stdenv ? "stdenv",
  componentTestsPrefix ? "",
  withInstrumentation ? false,
}@args:
let
  # Create a pkgs set with your httplib overlay applied.
  pkgs = import nixFlake.inputs.nixpkgs {
    inherit system;
    overlays = [ nixFlake.overlays.internal ];
  };
in
import ./. (
  args
  // {
    inherit pkgs;
    getStdenv = p: p.${stdenv};
    withSanitizers = withInstrumentation;
    withCoverage = withInstrumentation;
  }
)
