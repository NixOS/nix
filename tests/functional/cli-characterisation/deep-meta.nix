with import ../config.nix;
mkDerivation {
  name = "deep-meta-1.0";
  buildCommand = "mkdir -p $out";
  meta = {
    description = "Has deeply nested meta";
    nested = {
      a = {
        b = {
          c = "deep value";
        };
      };
    };
  };
}
