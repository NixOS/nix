# These overrides are applied to the dependencies of the Nix components.

{
  # Flake inputs; used for sources
  inputs,

  # The raw Nixpkgs, not affected by this scope
  pkgs,

  stdenv,
}:

let
  inherit (pkgs) lib;
in
scope: {
  inherit stdenv;

  mimalloc =
    if lib.versionAtLeast pkgs.mimalloc.version "3.5.1" then
      pkgs.mimalloc
    else
      pkgs.mimalloc.overrideAttrs rec {
        version = "3.5.1";
        src = pkgs.fetchFromGitHub {
          owner = "microsoft";
          repo = "mimalloc";
          tag = "v${version}";
          hash = "sha256-hljle/jR2hNvy2ikdOna9QZiyqzAuMjNUfC7vxl7g7k=";
        };
      };

  boehmgc =
    (pkgs.boehmgc.override {
      enableLargeConfig = true;
    }).overrideAttrs
      (attrs: {
        # Increase the initial mark stack size to avoid stack
        # overflows, since these inhibit parallel marking (see
        # GC_mark_some()). To check whether the mark stack is too
        # small, run Nix with GC_PRINT_STATS=1 and look for messages
        # such as `Mark stack overflow`, `No room to copy back mark
        # stack`, and `Grew mark stack to ... frames`.
        env.NIX_CFLAGS_COMPILE = "-DINITIAL_MARK_STACK_SIZE=1048576";
      });

  curl = pkgs.curl.override {
    http3Support = !pkgs.stdenv.hostPlatform.isWindows;
    # Make sure we enable all the dependencies for Content-Encoding/Transfer-Encoding decompression.
    zstdSupport = true;
    brotliSupport = true;
    zlibSupport = true;
  };

  libblake3 = pkgs.libblake3.override {
    useTBB =
      !(
        stdenv.hostPlatform.isWindows
        || stdenv.hostPlatform.isStatic
        # Some tbb tests fail with libc++.
        || (stdenv.cc.libcxx != null && stdenv.cc.libcxx.isLLVM)
      );
  };

  # Force the s2n TLS backend in aws-c-io on macOS; Apple SecureTransport is not
  # fork-safe and crashes daemon workers (NixOS/nix#15857). Override it across
  # the whole aws-c-* stack so one aws-c-io is shared.
  aws-crt-cpp =
    if !stdenv.hostPlatform.isDarwin then
      pkgs.aws-crt-cpp
    else
      let
        aws-c-io = pkgs.aws-c-io.overrideAttrs (old: {
          patches = (old.patches or [ ]) ++ [ ./patches/aws-c-io-s2n-darwin.patch ];
          cmakeFlags = old.cmakeFlags ++ [ "-DAWS_USE_SECITEM=OFF" ];
        });
        aws-c-http = pkgs.aws-c-http.override { inherit aws-c-io; };
        aws-c-auth = pkgs.aws-c-auth.override { inherit aws-c-io aws-c-http; };
        aws-c-event-stream = pkgs.aws-c-event-stream.override { inherit aws-c-io; };
        aws-c-mqtt = pkgs.aws-c-mqtt.override { inherit aws-c-io aws-c-http; };
        aws-c-s3 = pkgs.aws-c-s3.override { inherit aws-c-io aws-c-http aws-c-auth; };
      in
      pkgs.aws-crt-cpp.override {
        inherit
          aws-c-io
          aws-c-http
          aws-c-auth
          aws-c-event-stream
          aws-c-mqtt
          aws-c-s3
          ;
      };

  sqlite =
    if !stdenv.hostPlatform.isWindows then
      pkgs.sqlite
    else
      pkgs.sqlite.overrideAttrs (prevAttrs: {
        nativeBuildInputs = lib.filter (x: !(x.pname == "tcl")) prevAttrs.nativeBuildInputs or [ ];
        configureFlags = (lib.filter (x: !(lib.hasPrefix "--with-tcl" x)) prevAttrs.configureFlags) ++ [
          "--disable-tcl"
        ];
      });

  # TODO Hack until https://github.com/NixOS/nixpkgs/issues/45462 is fixed.
  boost =
    (pkgs.boost.override {
      extraB2Args = [
        "--with-container"
        "--with-context"
        "--with-coroutine"
        "--with-iostreams"
        "--with-url"
      ];
      enableIcu = false;
    }).overrideAttrs
      (old: {
        # Need to remove `--with-*` to use `--with-libraries=...`
        buildPhase = lib.replaceStrings [ "--without-python" ] [ "" ] old.buildPhase;
        installPhase = lib.replaceStrings [ "--without-python" ] [ "" ] old.installPhase;
      });
}
