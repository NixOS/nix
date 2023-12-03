{ lib
, fileset
, stdenv
, client
, daemon
}:

stdenv.mkDerivation {
  name =
    "nix-tests"
    + lib.optionalString
      (lib.versionAtLeast daemon.version "2.4pre20211005" &&
       lib.versionAtLeast client.version "2.4pre20211005")
      "-${client.version}-against-${daemon.version}";
}
