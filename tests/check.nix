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
    url = "file://" + toString ./lang/eval-okay-xml.exp.xml;
    sha256 = "0kg4sla7ihm8ijr8cb3117fhl99zrc2bwy1jrngsfmkh8bav4m0v";
  };
}
