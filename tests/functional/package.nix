{ lib
, stdenv
, mkMesonDerivation
, releaseTools

, meson
, ninja
, pkg-config
, rsync

, jq
, git
, mercurial
, util-linux

, nix-store
, nix-expr
, nix-cli

, rapidcheck
, gtest
, runCommand

, busybox-sandbox-shell ? null

# Configuration Options

, version

# For running the functional tests against a different pre-built Nix.
, test-daemon ? null
}:

let
  inherit (lib) fileset;
in

mkMesonDerivation (finalAttrs: {
  pname = "nix-functional-tests";
  inherit version;

  workDir = ./.;
  fileset = fileset.unions [
    ../../scripts/nix-profile.sh.in
    ../../.version
    ../../tests/functional
    ./.
  ];

  # Hack for sake of the dev shell
  passthru.baseNativeBuildInputs = [
    meson
    ninja
    pkg-config
    rsync

    jq
    git
    mercurial
  ] ++ lib.optionals stdenv.hostPlatform.isLinux [
    # For various sandboxing tests that needs a statically-linked shell,
    # etc.
    busybox-sandbox-shell
    # For Overlay FS tests need `mount`, `umount`, and `unshare`.
    # TODO use `unixtools` to be precise over which executables instead?
    util-linux
  ];

  nativeBuildInputs = finalAttrs.passthru.baseNativeBuildInputs ++ [
    nix-cli
  ];

  buildInputs = [
    nix-store
    nix-expr
  ];


  preConfigure =
    # "Inline" .version so it's not a symlink, and includes the suffix.
    # Do the meson utils, without modification.
    ''
      chmod u+w ./.version
      echo ${version} > ../../../.version
    ''
    # TEMP hack for Meson before make is gone, where
    # `src/nix-functional-tests` is during the transition a symlink and
    # not the actual directory directory.
    + ''
      cd $(readlink -e $PWD)
      echo $PWD | grep tests/functional
    '';

  mesonCheckFlags = [
    "--print-errorlogs"
  ];

  preCheck =
    # See https://github.com/NixOS/nix/issues/2523
    # Occurs often in tests since https://github.com/NixOS/nix/pull/9900
    lib.optionalString stdenv.hostPlatform.isDarwin ''
      export OBJC_DISABLE_INITIALIZE_FORK_SAFETY=YES
    '';

  doCheck = true;

  installPhase = ''
    mkdir $out
  '';

  meta = {
    platforms = lib.platforms.unix;
  };

} // lib.optionalAttrs (test-daemon != null) {
  NIX_DAEMON_PACKAGE = test-daemon;
})
