with import ./config.nix;
let
  dep = mkDerivation {
    name = "dep";
    buildCommand = ''
      mkdir $out; echo bla > $out/bla
    '';
  };
in
mkDerivation {
  name = "structured2";
  __structuredAttrs = true;
  outputs = [ "out" "dev" ];
  my.list = [ "a" "b" "c" ];
  exportReferencesGraph.refs = [ dep ];
  buildCommand = ''
    touch ''${outputs[out]}; touch ''${outputs[dev]}
  '';
}
