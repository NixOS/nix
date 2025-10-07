{
  nixFlake ? builtins.getFlake ("git+file://" + toString ../../..),
  system ? "x86_64-linux",
  withAWS ? null,
  withCurlS3 ? null,
}:

let
  pkgs = nixFlake.inputs.nixpkgs.legacyPackages.${system};
  lib = pkgs.lib;

  # Create base nixComponents using the flake's makeComponents
  baseNixComponents = nixFlake.lib.makeComponents {
    inherit pkgs;
  };

  # Override nixComponents if AWS parameters are specified
  nixComponents =
    if (withAWS == null && withCurlS3 == null) then
      baseNixComponents
    else
      baseNixComponents.overrideScope (
        final: prev: {
          nix-store = prev.nix-store.override (
            lib.optionalAttrs (withAWS != null) { inherit withAWS; }
            // lib.optionalAttrs (withCurlS3 != null) { inherit withCurlS3; }
          );
        }
      );

  # Import NixOS tests with the overridden nixComponents
  tests = import ../../../tests/nixos {
    inherit lib pkgs nixComponents;
    nixpkgs = nixFlake.inputs.nixpkgs;
    inherit (nixFlake.inputs) nixpkgs-23-11;
  };
in
{
  inherit (tests)
    functional_user
    githubFlakes
    nix-docker
    tarballFlakes
    ;
}
