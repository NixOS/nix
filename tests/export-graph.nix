with import ./config.nix;

rec {

  buildGraphBuilder = builtins.toFile "build-graph-builder"
    ''
      #cat refs
      while read path; do
          read drv
          read nrRefs
          echo "$path has $nrRefs references"
          echo -n "$path" >> $out
          for ((n = 0; n < $nrRefs; n++)); do read ref; echo "ref $ref"; done
      done < refs
    '';

  buildGraph = mkDerivation {
    name = "dependencies";
    builder = buildGraphBuilder;
    exportReferencesGraph = ["refs" (import ./dependencies.nix)];
  };

}
