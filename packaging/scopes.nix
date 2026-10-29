{
  lib,
  officialRelease,
  src,
}:
rec {
  packageSetsFor =
    args@{
      pkgs,
      getStdenv ? pkgs: pkgs.stdenv,
    }:
    let
      nixComponentsSplices = lib.mapCrossIndex (
        pkgs': (packageSetsFor (args // { pkgs = pkgs'; })).nixComponents
      ) (lib.renameCrossIndexFrom "pkgs" pkgs);
      nixDependenciesSplices = lib.mapCrossIndex (
        pkgs': (packageSetsFor (args // { pkgs = pkgs'; })).nixDependencies
      ) (lib.renameCrossIndexFrom "pkgs" pkgs);

      # A new scope, so that we can use `callPackage` to inject our own interdependencies
      # without "polluting" the top level "`pkgs`" attrset.
      # This also has the benefit of providing us with a distinct set of packages
      # we can iterate over.
      nixComponents =
        lib.makeScopeWithSplicing'
          {
            inherit (pkgs) splicePackages;
            inherit (nixDependencies) newScope;
          }
          {
            otherSplices = lib.renameCrossIndexTo "self" nixComponentsSplices;
            f = import ./components.nix {
              inherit (pkgs) lib;
              inherit officialRelease;
              inherit pkgs;
              inherit src;
              maintainers = [ ];
            };
          };

      # The dependencies are in their own scope, so that they don't have to be
      # in Nixpkgs top level `pkgs` or `nixComponents`.
      nixDependencies =
        lib.makeScopeWithSplicing'
          {
            inherit (pkgs) splicePackages;
            inherit (pkgs) newScope; # layered directly on pkgs, unlike nixComponents above
          }
          {
            otherSplices = lib.renameCrossIndexTo "self" nixDependenciesSplices;
            f = import ./dependencies.nix {
              inherit pkgs;
              stdenv = getStdenv pkgs;
            };
          };

      # If the package set is largely empty, we should(?) return empty sets
      # This is what most package sets in Nixpkgs do. Otherwise, we get
      # an error message that indicates that some stdenv attribute is missing,
      # and indeed it will be missing, as seemingly `pkgsTargetTarget` is
      # very incomplete.
      fixup = lib.mapAttrs (k: v: if !(pkgs ? newScope) then { } else v);
    in
    fixup {
      inherit nixDependencies;
      inherit nixComponents;
    };
}
