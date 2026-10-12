# Regression test for the relative-size `//` layering trigger and the
# raised `maxLayers` (8->16): each overlay is deliberately bigger than
# the absolute threshold (16), so layering can only happen via the
# relative-size comparison this adds, and folding enough of them chains
# the layer depth past the *old* cap of 8 into the newly-reachable 9-16
# range. Combined with `rec` self-reference and a dynamic attr name,
# since neither is exercised by the pre-existing
# eval-okay-regrettable-rec-attrset-merge.nix (that test merges via
# dotted-attrpath splicing, not `//`, and never reaches this code path).
let
  mkOverlay =
    i:
    rec {
      "dyn${toString i}" = i;
      base = i * 10;
      selfRef = base + 1;
    }
    // builtins.listToAttrs (
      map (j: {
        name = "pad${toString i}_${toString j}";
        value = j;
      }) (builtins.genList (j: j) 17)
    );

  final = builtins.foldl' (acc: i: acc // (mkOverlay i)) { } (builtins.genList (i: i) 12);
in
[
  (builtins.length (builtins.attrNames final))
  final.selfRef
  final.dyn11
  final.pad11_5
]
