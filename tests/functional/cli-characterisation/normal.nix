with import ../config.nix;
mkDerivation {
  name = "normal-1.0";
  buildCommand = "mkdir -p $out";
  meta = {
    description = "A normal package";
    homepage = "https://example.com";
    license = "MIT";
  };
}
