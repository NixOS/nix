{
  paths = rec {
    D = builtins.toFile "D" "dependency 0";
    S = builtins.toFile "S" D;
    F = builtins.toFile "F" D;
    E = builtins.toFile "E" F;
    C = builtins.toFile "C" E;
    X = builtins.toFile "X" "${S} ${C}";
    A = builtins.toFile "A" X;
    B = builtins.toFile "B" C;
    P = builtins.toFile "P" "${A} ${B}";
  };
}
