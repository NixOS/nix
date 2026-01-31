with import ./config.nix;

{
  silentA = mkDerivation {
    name = "serve-max-silent-time-a";
    buildCommand = ''
      touch $out
      sleep 6
    '';
  };

  silentB = mkDerivation {
    name = "serve-max-silent-time-b";
    buildCommand = ''
      touch $out
      sleep 6
    '';
  };
}
