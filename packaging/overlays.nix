{
  overlayFor,
  packageSetsFor,
}:
{
  internal = overlayFor (p: p.stdenv);

  /**
    A Nixpkgs overlay that sets `nix` to something like `packages.<system>.nix-everything`,
    except dependencies aren't taken from (flake) `nix.inputs.nixpkgs`, but from the Nixpkgs packages
    where the overlay is used.
  */
  default =
    final: prev:
    let
      packageSets = packageSetsFor { pkgs = final; };
    in
    {
      nix = packageSets.nixComponents.nix-everything;
    };
}
