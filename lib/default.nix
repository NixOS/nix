rec {

  inherit (import /home/eelco/nixpkgs/pkgs/system/i686-linux.nix) stdenv;

  compileC = {main, localIncludes ? []}: stdenv.mkDerivation {
    name = "compile-c";
    builder = ./compile-c.sh;
    localIncludes =
      if localIncludes == "auto" then
        import (findIncludes {main = toString main; hack = curTime;})
      else
        localIncludes;
    inherit main;
  };

  /*
  runCommand = {command}: {
    name = "run-command";
    builder = ./run-command.sh;
    inherit command;
  };
  */

  findIncludes = {main, hack}: stdenv.mkDerivation {
    name = "find-includes";
    builder = ./find-includes.sh;
    inherit main hack;
  };
  
  link = {objects, programName ? "program"}: stdenv.mkDerivation {
    name = "link";
    builder = ./link.sh;
    inherit objects programName;
  };

}
