# Hydra jobset containing only the artifacts consumed by
# `maintainers/upload-release.pl`, so a release can be cut without
# waiting on the full `hydraJobs` CI matrix.
#
# Evaluated as a legacy (non-flake) jobset because Hydra hard-codes flake
# jobsets to `outputs.hydraJobs`; we re-enter the flake via
# `builtins.getFlake` so derivations stay identical to the flake jobset
# and share builds through the binary cache.
#
# Hydra jobset configuration:
#   Type:            Legacy
#   Nix expression:  packaging/release-jobs.nix in input `src`
#   Inputs:
#     src  (Git checkout)  https://github.com/NixOS/nix <branch>
{
  src ? {
    outPath = ./..;
  },
}:
let
  # Fetch by GitHub ref rather than the bare store path Hydra hands us,
  # so `rev`/`lastModified` (and thus the version suffix) match the flake
  # jobset and derivations are shared.
  flake = builtins.getFlake (
    if src ? rev then
      "github:NixOS/nix/${src.rev}"
    else
      # Local evaluation / testing.
      builtins.unsafeDiscardStringContext (toString src)
  );
  inherit (flake) hydraJobs;
  inherit (flake.inputs.nixpkgs) lib;

  jobs = {
    # `nix-everything` per system: provides the store paths for
    # `fallback-paths.nix` and (on x86_64-linux) the rendered manual via
    # its `doc` output.
    build.nix-everything = hydraJobs.build.nix-everything;
    buildCross.nix-everything = hydraJobs.buildCross.nix-everything;

    inherit (hydraJobs)
      manual
      binaryTarball
      binaryTarballCross
      installerScript
      installerScriptForGHA
      dockerImage
      ;

    # Aggregate gating job: green ⇒ every artifact the upload script
    # needs is available.  `upload-release` can wait on this single job
    # instead of the whole evaluation.  Constituents are referenced by
    # job *name* so that an evaluation failure in one of them does not
    # take down the aggregate's own evaluation.
    release = flake.inputs.nixpkgs.legacyPackages.x86_64-linux.releaseTools.aggregate {
      name = "nix-release-${flake.packages.x86_64-linux.nix-everything.version}";
      meta.description = "Artifacts required for a Nix release";
      constituents =
        let
          collectJobNames =
            prefix: x:
            if lib.isDerivation x then
              [ prefix ]
            else if lib.isAttrs x then
              lib.concatLists (
                lib.mapAttrsToList (n: collectJobNames (if prefix == "" then n else "${prefix}.${n}")) x
              )
            else
              [ ];
        in
        collectJobNames "" (builtins.removeAttrs jobs [ "release" ]);
    };
  };
in
jobs
