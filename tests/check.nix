{checkBuildId ? 0}:

with import ./config.nix;

{
  nondeterministic = mkDerivation {
    inherit checkBuildId;
    name = "nondeterministic";
    buildCommand =
      ''
        mkdir $out
        date +%s.%N > $out/date
        echo "CHECK_TMPDIR=$TMPDIR"
        echo "checkBuildId=$checkBuildId"
        echo "$checkBuildId" > $TMPDIR/checkBuildId
      '';
  };

  deterministic = mkDerivation {
    inherit checkBuildId;
    name = "deterministic";
    buildCommand =
      ''
        mkdir $out
        echo date > $out/date
        echo "CHECK_TMPDIR=$TMPDIR"
        echo "checkBuildId=$checkBuildId"
        echo "$checkBuildId" > $TMPDIR/checkBuildId
      '';
  };

  failed = mkDerivation {
    inherit checkBuildId;
    name = "failed";
    buildCommand =
      ''
        mkdir $out
        echo date > $out/date
        echo "CHECK_TMPDIR=$TMPDIR"
        echo "checkBuildId=$checkBuildId"
        echo "$checkBuildId" > $TMPDIR/checkBuildId
        false
      '';
  };

  hashmismatch = import <nix/fetchurl.nix> {
    url = "file://" + builtins.getEnv "TMPDIR" + "/dummy";
    sha256 = "0mdqa9w1p6cmli6976v4wi0sw9r4p5prkj7lzfd1877wk11c9c73";
  };

  fetchurl = import <nix/fetchurl.nix> {
    url = "file://" + toString ./lang/eval-okay-xml.exp.xml;
    sha256 = "0kg4sla7ihm8ijr8cb3117fhl99zrc2bwy1jrngsfmkh8bav4m0v";
  };
}
