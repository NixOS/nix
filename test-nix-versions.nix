{ lib
, fileset
, stdenv
, client
, daemon
}:

stdenv.mkDerivation {
  NIX_DAEMON_PACKAGE = daemon;
  NIX_CLIENT_PACKAGE = client;
  name =
    "nix-tests"
    + lib.optionalString
      (lib.versionAtLeast daemon.version "2.4pre20211005" &&
       lib.versionAtLeast client.version "2.4pre20211005")
      "-${client.version}-against-${daemon.version}";

  inherit (client)
    version
    VERSION_SUFFIX
    nativeBuildInputs
    buildInputs
    propagatedBuildInputs
    ;

  src = fileset.toSource {
    root = ./.;
    fileset = with client.passthru.filesets;
      fileset.intersect baseFiles (fileset.unions [
        configureFiles
        topLevelBuildFiles
        functionalTestFiles
      ]);
  };

  configureFlags = client.configureFlags # otherwise configure fails
    ++ [ "--disable-build" ];

  dontBuild = true;
  doInstallCheck = true;

  installPhase = ''
    mkdir -p $out
  '';

  installCheckPhase = ''
    mkdir -p src/nix-channel
    make installcheck -j$NIX_BUILD_CORES -l$NIX_BUILD_CORES
  '';
}
