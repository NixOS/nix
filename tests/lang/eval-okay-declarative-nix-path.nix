let
  nixDir = (builtins.head (builtins.filter (x: (x.prefix or "") == "nix") __curSettings.nix-path)).value;
in builtins.importWithSettings {
  nix-path = [ { prefix = "this"; value = ./test-nix-path.nix; } { prefix = "nix"; value = nixDir; } ];
} ./test-nix-path.nix
