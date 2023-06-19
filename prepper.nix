/*
  prepper.nix - mediam grained incremental build while RFC 92 is in progress

  This splits the build into separate derivations that build each component, so
  that changes to the higher-level components don't need a rebuild of the
  components they depend on.
  It achieves this by overriding the simpler original derivation that would
  build everything in one go.

  It does so by saving the working directory in the output of the intermediate
  steps.

  Flaws:
   - The dependency management and source filtering are not super accurate
     while any mistakes will be caught by the build, it still includes many
     files that are not needed for the build.
   - Files are included in every component build by default. This means that
     changes to the build graph generally work, but may rebuild more than
     necessary.
   - Installed files from outputs are not accounted for. The docs build relies
     on the installed nix binary. This means linking the nix binary in the docs
     build.
   - Object files could be built separately, so that their library dependency is
     only on headers. This will save more rebuilds.
   - A component might rebuild its dependencies. So this is fail safe in the
     sense that you don't have to bother with the problem immediately, but it
     does slow down the build and you'd have to check the logs to make sure it
     doesn't happen.

  What we should do:
   - RFC 92. That will let us replace this by a principled solution that is
     more accurate, more efficient, and easier to maintain.
   - Low hanging fruit: support installed files in the components.
   - Short of that, we could make the libraries into individual packages. This
     way we won't have to copy as many files around.
*/

{ pkgs }:

let
  inherit (pkgs) lib;

  composeOverrides = f: g: x:
    let x2 = g x;
    in x2 // f (x // x2);

  # removeFile = f: o: {
  #   buildPhase = o.buildPhase + ''
  #     rm ${f}
  #   '';
  # };

  prep = { drv, componentSpecs }:
    let
      depsClosure =
        let clList = lib.genericClosure {
              startSet = #
                lib.concatLists (lib.mapAttrsToList
                  (name: spec@{deps ? [], ...}: map (dep: { inherit name dep; key = "${name} ---> ${dep}"; }) deps)
                  componentSpecs);
              operator = item: map (dep: { inherit (item) name; inherit dep; key = "${item.name} ---> ${dep}";}) componentSpecs.${item.name}.deps or [];
            };
        in
          # Ensure an empty attr exists for each component
          lib.mapAttrs (name: _: []) componentSpecs
          # Add the dependencies
          // lib.zipAttrs (
            map
              (item: { ${item.name} = item.dep; })
              clList
          );

      nonDeps =
        lib.mapAttrs
          (name: deps: lib.attrNames (builtins.removeAttrs componentSpecs ([name] ++ deps)))
          depsClosure;

      components =
        lib.mapAttrs
          (name: spec@{ dirs, deps ? [], attrsOverride ? (o: {}), findFilter ? "", ... }:
            let prebuiltDeps = lib.genAttrs deps (x: components.${x});
            in
            ((addPrebuilt drv prebuiltDeps).overrideAttrs (o: {

              # Remove the reverse dependencies
              src = lib.cleanSourceWith {
                src = o.src;
                filter = path: type:
                  lib.all
                    (dep:
                      let depSpec = componentSpecs.${dep};
                      in
                        lib.all
                          (src:
                            toString path != toString (o.src.origSrc + ("/" + src)))
                          depSpec.dirs
                    )
                    (nonDeps.${name});
              };
              preBuild = ''
                ${o.preBuild or ""}
                buildFlagsArray+=( missingMakefiles=ok ${spec.targets or "default"} )
                echo "Building component ${name}..."
              '';
              # TODO hook into setup.sh like in preBuild above
              # checkPhase = lib.optionalString (spec?checkTargets) ''
              #   make -j $NIX_BUILD_CORES missingMakefiles=ok $makeFlags ${spec.checkTargets}
              # '';
              installPhase = ''
                runHook preInstall
                for f in $(find ${lib.escapeShellArgs dirs} -type f ${findFilter}); do
                  mkdir -p $(dirname $out/progress/$f)
                  install $f $out/progress/$f
                done
                runHook postInstall
              '';
              # doInstallCheck can be whatever it needs to be so that it's as much as
              # possible like a normal build.
              installCheckPhase = ":";
            })
            ).overrideAttrs attrsOverride
          )
          componentSpecs;

      copyPrebuilt = layer: ''
        for f in $(cd ${layer}/progress; find . -type f); do
          if [[ -e $f ]]; then continue; fi
          echo Copying pre-built $f
          mkdir -p $(dirname $f)
          ${lib.getExe pkgs.buildPackages.perl} -pe "s|\Q${layer}\E|$out|g" \
            < ${layer}/progress/$f > $f
          [[ -x ${layer}/progress/$f ]] && chmod +x $f
          touch --reference=timestamper $f
        done
      '';

      addPrebuilt = drv: deps:
        drv.overrideAttrs (o: {
          postConfigure = ''
            ${o.postConfigure or ""}
            touch timestamper
            ${lib.concatMapStringsSep "\n" copyPrebuilt (lib.attrValues deps)}
            rm timestamper
          '';
          disallowedReferences = lib.attrValues deps;
          passthru = o.passthru // {
            prebuiltDeps = deps;
          };
        });

      toplevel = addPrebuilt drv components;

    in {
      inherit components copyPrebuilt toplevel;
    };
in
{
  inherit
    composeOverrides
    prep
    ;
}

  # Unused:

  # revDeps =
  #   lib.zipAttrs (
  #     lib.concatLists (
  #       lib.mapAttrsToList
  #         (name: spec@{ deps ? [], ... }:
  #           map
  #             (dep: { ${dep} = name; })
  #             deps
  #         )
  #         componentSpecs
  #     )
  #   );

  # revDepsClosure =
  #   let clList = lib.genericClosure {
  #         startSet = #
  #           lib.concatLists (lib.mapAttrsToList
  #             (name: rds: map (rd: { inherit name rd; key = "${name} <--- ${rd}"; }) rds)
  #             revDeps);
  #         operator = item: map (rd: { inherit (item) name; inherit rd; key = "${item.name} <--- ${rd}";}) revDeps.${item.rd} or [];
  #       };
  #   in lib.zipAttrs (
  #       map
  #         (item: { ${item.name} = item.rd; })
  #         clList
  #     );
