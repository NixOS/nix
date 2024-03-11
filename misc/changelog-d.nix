# Taken temporarily from <nixpkgs/pkgs/by-name/ch/changelog-d/package.nix>
{
  callPackage,
  lib,
  haskell,
  haskellPackages,
}:

let
  hsPkg = haskellPackages.callPackage ./changelog-d.cabal.nix { };

  addCompletions = haskellPackages.generateOptparseApplicativeCompletions ["changelog-d"];

  haskellModifications =
    lib.flip lib.pipe [
      addCompletions
      haskell.lib.justStaticExecutables
    ];

  mkDerivationOverrides = finalAttrs: oldAttrs: {

    version = oldAttrs.version + "-git-${lib.strings.substring 0 7 oldAttrs.src.rev}";

    meta = oldAttrs.meta // {
      homepage = "https://codeberg.org/roberth/changelog-d";
      maintainers = [ lib.maintainers.roberth ];
    };

  };
in
  (haskellModifications hsPkg).overrideAttrs mkDerivationOverrides
