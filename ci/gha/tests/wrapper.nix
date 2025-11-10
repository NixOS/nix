{
  nixFlake ? builtins.getFlake ("git+file://" + toString ../../..),
  system ? builtins.currentSystem,
  pkgs ? nixFlake.inputs.nixpkgs.legacyPackages.${system},
  stdenv ? "stdenv",
  componentTestsPrefix ? "",
  withInstrumentation ? false,
}@args:
import ./. (
  args
  // {
    getStdenv = p: p.${stdenv};
    withSanitizers = withInstrumentation;
    withCoverage = withInstrumentation;
  }
)
