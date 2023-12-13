{ lib
, stdenv
, which
, lowdown-src
}:

stdenv.mkDerivation rec {
  name = "lowdown-0.9.0";

  src = lowdown-src;

  outputs = [ "out" "bin" "dev" ];

  nativeBuildInputs = [ which ];

  configurePhase = ''
      ${lib.optionalString (stdenv.isDarwin && stdenv.isAarch64) "echo \"HAVE_SANDBOX_INIT=false\" > configure.local"}
      ./configure \
        PREFIX=${placeholder "dev"} \
        BINDIR=${placeholder "bin"}/bin
  '';
}
