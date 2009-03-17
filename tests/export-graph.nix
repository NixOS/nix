with import ./config.nix;

rec {

  printRefs =
    ''
      while read path; do
          read drv
          read nrRefs
          echo "$path has $nrRefs references"
          echo "$path" >> $out
          for ((n = 0; n < $nrRefs; n++)); do read ref; echo "ref $ref"; done
      done < refs
    '';

  runtimeGraph = mkDerivation {
    name = "dependencies";
    builder = builtins.toFile "build-graph-builder" "${printRefs}";
    exportReferencesGraph = ["refs" (import ./dependencies.nix)];
  };

  buildGraph = mkDerivation {
    name = "dependencies";
    builder = builtins.toFile "build-graph-builder" "${printRefs}";
    exportBuildReferencesGraph = ["refs" (import ./dependencies.nix).drvPath];
  };

}
