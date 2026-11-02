# test //-layering with sizes and chains beyond the thresholds as well
# as dynamic+rec attrsets
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
