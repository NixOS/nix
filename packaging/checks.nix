{
  lib,
  self,
  nixpkgsFor,
  flatMapAttrs,
  forAllSystems,
  linux32BitSystems,
  linux64BitSystems,
  devFlake,
}:
forAllSystems (
  system:
  (import ../ci/gha/tests {
    inherit system;
    pkgs = nixpkgsFor.${system}.native;
    nixFlake = self;
  }).topLevel
  // (lib.optionalAttrs (builtins.elem system linux64BitSystems)) {
    dockerImage = self.hydraJobs.dockerImage.${system};
  }
  // (lib.optionalAttrs (!(builtins.elem system linux32BitSystems))) {
    # Some perl dependencies are broken on i686-linux.
    # Since the support is only best-effort there, disable the perl
    # bindings
    perlBindings = self.hydraJobs.perlBindings.${system};
  }
  # Add "passthru" tests
  //
    flatMapAttrs
      {
        "" = {
          pkgs = nixpkgsFor.${system}.native;
        };
      }
      (
        nixpkgsPrefix: args:
        (import ../ci/gha/tests (
          args
          // {
            nixFlake = self;
            componentTestsPrefix = nixpkgsPrefix;
          }
        )).componentTests
      )
  // devFlake.checks.${system} or { }
)
