rec {

  # Should point at your Nixpkgs installation.
  pkgPath = ./pkgs;

  pkgs = import (pkgPath + /system/all-packages.nix) {};

  stdenv = pkgs.stdenv;
  

  compileC = {main, localIncludes ? "auto", cFlags ? "", sharedLib ? false}:
  stdenv.mkDerivation {
    name = "compile-c";
    builder = ./compile-c.sh;
    localIncludes =
      if localIncludes == "auto" then
        dependencyClosure {
          scanner = main:
            import (findIncludes {
              inherit main;
            });
          startSet = [main];
        }
      else
        localIncludes;
    inherit main;
    cFlags = [
      cFlags
      (if sharedLib then ["-fpic"] else [])
    ];
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
    realBuilder = pkgs.perl ~ "bin/perl";
    args = [ ./find-includes.pl ];
    inherit main;
  };
  
  link = {objects, programName ? "program", libraries ? []}: stdenv.mkDerivation {
    name = "link";
    builder = ./link.sh;
    inherit objects programName libraries;
  };

  makeLibrary = {objects, libraryName ? [], sharedLib ? false}:
  # assert sharedLib -> fold (obj: x: assert obj.sharedLib && x) false objects
  stdenv.mkDerivation {
    name = "library";
    builder = ./make-library.sh;
    inherit objects libraryName sharedLib;
  };

}
