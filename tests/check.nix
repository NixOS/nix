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

  hashmismatch = import <nix/fetchurl.nix> {
    url = "file://" + toString ./dummy;
    sha256 = "0mdqa9w1p6cmli6976v4wi0sw9r4p5prkj7lzfd1877wk11c9c73";
  };

  fetchurl = import <nix/fetchurl.nix> {
    url = "file://" + toString ./lang/eval-okay-xml.exp.xml;
    sha256 = "0kg4sla7ihm8ijr8cb3117fhl99zrc2bwy1jrngsfmkh8bav4m0v";
  };
}
