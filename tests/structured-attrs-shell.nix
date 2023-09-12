with import ./config.nix;
let
  dep = mkDerivation {
    name = "dep";
    buildCommand = ''
      mkdir $out; echo bla > $out/bla
    '';
  };
  inherit (import ./shell.nix { inNixShell = true; }) stdenv;
in
mkDerivation {
  name = "structured2";
  __structuredAttrs = true;
  inherit stdenv;
  outputs = [ "out" "dev" ];
  my.list = [ "a" "b" "c" ];
  exportReferencesGraph.refs = [ dep ];
  buildCommand = ''
    touch ''${outputs[out]}; touch ''${outputs[dev]}
  '';
}
