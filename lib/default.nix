rec {

  inherit (import /home/eelco/nixpkgs/pkgs/system/i686-linux.nix) stdenv;

  compileC = {main, localIncludes ? []}: stdenv.mkDerivation {
    name = "compile-c";
    builder = ./compile-c.sh;
    inherit main localIncludes;
  };

  link = {objects}: stdenv.mkDerivation {
    name = "link";
    builder = ./link.sh;
    inherit objects;
  };
  
}
