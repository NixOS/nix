rec {

  inherit (import /home/eelco/nixpkgs/pkgs/system/i686-linux.nix) stdenv;

  compileC = {main, localIncludes ? []}: stdenv.mkDerivation {
    name = "compile-c";
    builder = ./compile-c.sh;
    inherit main localIncludes;
  };

  /*
  runCommand = {command}: {
    name = "run-command";
    builder = ./run-command.sh;
    inherit command;
  };
  */

  findIncludes = {main}: stdenv.mkDerivation {
    name = "find-includes";
    builder = ./find-includes.sh;
    inherit main;
  };
  
  link = {objects, programName ? "program"}: stdenv.mkDerivation {
    name = "link";
    builder = ./link.sh;
    inherit objects programName;
  };

}
