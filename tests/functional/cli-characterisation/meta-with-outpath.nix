with import ../config.nix;
mkDerivation {
  name = "meta-with-outpath-1.0";
  buildCommand = "mkdir -p $out";
  meta = {
    description = "Has outPath in meta";
    bad = {
      outPath = "/nix/store/fake";
    };
  };
}
