rec {

  inherit (import /home/eelco/nixpkgs/pkgs/system/i686-linux.nix) stdenv;

  compileC = {main, localIncludes ? [], cFlags ? ""}: stdenv.mkDerivation {
    name = "compile-c";
    builder = ./compile-c.sh;
    localIncludes =
      if localIncludes == "auto" then
        import (findIncludes {
          main = toString main;
          hack = curTime;
          inherit cFlags;
        })
      else
        localIncludes;
    inherit main cFlags;
  };

  /*
  runCommand = {command}: {
    name = "run-command";
    builder = ./run-command.sh;
    inherit command;
  };
  */

  findIncludes = {main, hack, cFlags ? ""}: stdenv.mkDerivation {
    name = "find-includes";
    builder = ./find-includes.sh;
    inherit main hack cFlags;
  };
  
  link = {objects, programName ? "program", libraries ? []}: stdenv.mkDerivation {
    name = "link";
    builder = ./link.sh;
    inherit objects programName libraries;
  };

  makeLibrary = {objects, libraryName ? []}: stdenv.mkDerivation {
    name = "library";
    builder = ./make-library.sh;
    inherit objects libraryName;
  };

}
