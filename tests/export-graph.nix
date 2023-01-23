with import ./config.nix;

rec {

  printRefs =
    ''
      echo $exportReferencesGraph
      while read path; do
          read drv
          read nrRefs
          echo "$path has $nrRefs references"
          echo "$path" >> $out
          for ((n = 0; n < $nrRefs; n++)); do read ref; echo "ref $ref"; test -e "$ref"; done
      done < refs
    '';

  foo."bar.runtimeGraph" = mkDerivation {
    name = "dependencies";
    builder = builtins.toFile "build-graph-builder" "${printRefs}";
    exportReferencesGraph = ["refs" (import ./dependencies.nix)];
  };

  foo."bar.buildGraph" = mkDerivation {
    name = "dependencies";
    builder = builtins.toFile "build-graph-builder" "${printRefs}";
    exportReferencesGraph = ["refs" (import ./dependencies.nix).drvPath];
  };

  foo."bar.buildGraphWithoutOutDeps" = let
    dep-a = mkDerivation {
      name = "dep-a";
      buildCommand = "mkdir $out; echo foo > $out/bar";
    };

    dep-b = mkDerivation rec {
      name = "dep-b";
      buildCommand = builtins.unsafeDiscardOutputDependency "mkdir $out; echo ${dep-a.drvPath} > $out/bar";
    };
  in mkDerivation {
    name = "dependencies";
    builder = builtins.toFile "build-graph-builder" "${printRefs}";
    exportReferencesGraph = ["refs" dep-b.drvPath];
  };

}
