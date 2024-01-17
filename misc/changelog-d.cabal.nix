{ mkDerivation, aeson, base, bytestring, cabal-install-parsers
, Cabal-syntax, containers, directory, filepath, frontmatter
, generic-lens-lite, lib, mtl, optparse-applicative, parsec, pretty
, regex-applicative, text, pkgs
}:
let rev = "f30f6969e9cd8b56242309639d58acea21c99d06";
in
mkDerivation {
  pname = "changelog-d";
  version = "0.1";
  src = pkgs.fetchurl {
    name = "changelog-d-${rev}.tar.gz";
    url = "https://codeberg.org/roberth/changelog-d/archive/${rev}.tar.gz";
    hash = "sha256-8a2+i5u7YoszAgd5OIEW0eYUcP8yfhtoOIhLJkylYJ4=";
  } // { inherit rev; };
  isLibrary = false;
  isExecutable = true;
  libraryHaskellDepends = [
    aeson base bytestring cabal-install-parsers Cabal-syntax containers
    directory filepath frontmatter generic-lens-lite mtl parsec pretty
    regex-applicative text
  ];
  executableHaskellDepends = [
    base bytestring Cabal-syntax directory filepath
    optparse-applicative
  ];
  doHaddock = false;
  description = "Concatenate changelog entries into a single one";
  license = lib.licenses.gpl3Plus;
  mainProgram = "changelog-d";
}
