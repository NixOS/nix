with import ./config.nix;

{
  nondeterministic = mkDerivation {
    name = "nondeterministic";
    buildCommand =
      ''
        mkdir $out
        date +%s.%N > $out/date
      '';
  };
}
