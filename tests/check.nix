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

  fetchurl = import <nix/fetchurl.nix> {
    url = "file://" + toString ./lang/eval-okay-xml.out;
    sha256 = "426fefcd2430e986551db13fcc2b1e45eeec17e68ffeb6ff155be2f8aaf5407e";
  };
}
