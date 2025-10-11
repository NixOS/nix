{
  nixFlake ? builtins.getFlake ("git+file://" + toString ../../..),
  system ? builtins.currentSystem,
  pkgs ? nixFlake.inputs.nixpkgs.legacyPackages.${system},
  stdenv ? "stdenv",
  componentTestsPrefix ? "",
  withInstrumentation ? false,
  withCurlS3 ? null,
}@args:
import ./. (
  args
  // {
    getStdenv = p: p.${stdenv};
    withSanitizers = withInstrumentation;
    withCoverage = withInstrumentation;
    inherit withCurlS3;
  }
)
