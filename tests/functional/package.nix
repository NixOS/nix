{
  lib,
  stdenv,
  mkMesonDerivation,

  meson,
  ninja,
  pkg-config,

  jq,
  git,
  mercurial,
  unixtools,
  util-linux,

  nix-store,
  nix-expr,
  nix-cli,

  busybox-sandbox-shell ? null,

  # Configuration Options

  pname ? "nix-functional-tests",
  version,

  # For running the functional tests against a different pre-built Nix.
  test-daemon ? null,
}:

let
  inherit (lib) fileset;
in

mkMesonDerivation (
  finalAttrs:
  {
    inherit pname version;

    workDir = ./.;
    fileset = fileset.unions [
      ../../nix-meson-build-support
      ../../scripts/nix-profile.sh.in
      ../../.version
      ../../tests/functional
      ./.
    ];

    nativeBuildInputs = [
      meson
      ninja
      pkg-config

      jq
      git
      mercurial
      unixtools.script

      # Explicitly splice the hostHost variant to fix LLVM tests. The nix-cli
      # has to be in PATH, but must come from the host context where it's built
      # with libc++.
      (nix-cli.__spliced.hostHost or nix-cli)
    ]
    ++ lib.optionals stdenv.hostPlatform.isLinux [
      # For various sandboxing tests that needs a statically-linked shell,
      # etc.
      busybox-sandbox-shell
      # For Overlay FS tests need `mount`, `umount`, and `unshare`.
      # For `script` command (ensuring a TTY)
      # TODO use `unixtools` to be precise over which executables instead?
      util-linux
    ];

    buildInputs = [
      nix-store
      nix-expr
    ];

    preConfigure =
      # TEMP hack for Meson before make is gone, where
      # `src/nix-functional-tests` is during the transition a symlink and
      # not the actual directory directory.
      ''
        cd $(readlink -e $PWD)
        echo $PWD | grep tests/functional
      '';

    mesonCheckFlags = [
      "--print-errorlogs"
    ];

    doCheck = true;

    installPhase = ''
      mkdir $out
    '';

    meta = {
      platforms = lib.platforms.unix;
    };

  }
  // lib.optionalAttrs (test-daemon != null) {
    # TODO rename to _NIX_TEST_DAEMON_PACKAGE
    NIX_DAEMON_PACKAGE = test-daemon;
    _NIX_TEST_CLIENT_VERSION = nix-cli.version;
  }
)
