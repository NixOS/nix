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
    if lib.versionAtLeast pkgs.mimalloc.version "3.3.2" then
      pkgs.mimalloc
    else
      pkgs.mimalloc.overrideAttrs rec {
        version = "3.3.2";
        src = pkgs.fetchFromGitHub {
          owner = "microsoft";
          repo = "mimalloc";
          tag = "v${version}";
          hash = "sha256-GZ37qQVDe9jgMb4Coe5oKvgaLTspZDlSkS5rdy1MfUU=";
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
        NIX_CFLAGS_COMPILE = "-DINITIAL_MARK_STACK_SIZE=1048576";
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
          patches = (old.patches or [ ]) ++ [ ./aws-c-io-s2n-darwin.patch ];
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

  libgit2 =
    if lib.versionAtLeast pkgs.libgit2.version "1.9.4" then
      pkgs.libgit2
    else
      # Grab newer libgit2.
      pkgs.libgit2.overrideAttrs rec {
        version = "1.9.4";
        src = pkgs.fetchFromGitHub {
          owner = "libgit2";
          repo = "libgit2";
          tag = "v${version}";
          hash = "sha256-ZKUiz3pdFE2SKxh53X2oyr7hs32Njj5YVA0OXDXz7h0=";
        };
      };

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
