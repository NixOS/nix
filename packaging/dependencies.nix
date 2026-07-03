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

  boehmgc =
    (pkgs.boehmgc.override {
      enableLargeConfig = true;
      inherit stdenv;
    }).overrideAttrs
      (attrs: {
        # Reduce contention on the GC allocation lock during parallel
        # evaluation by handing out multiple heap blocks worth of
        # objects per lock acquisition in GC_generic_malloc_many().
        # The default batch size is set via GC_MANY_BLOCKS_DEFAULT
        # below and can be overridden at runtime through the
        # GC_MALLOC_MANY_BLOCKS environment variable.
        patches = (attrs.patches or [ ]) ++ [ ./patches/boehmgc-batch-malloc-many.patch ];

        env = (attrs.env or { }) // {
          # Increase the initial mark stack size to avoid stack
          # overflows, since these inhibit parallel marking (see
          # GC_mark_some()). To check whether the mark stack is too
          # small, run Nix with GC_PRINT_STATS=1 and look for messages
          # such as `Mark stack overflow`, `No room to copy back mark
          # stack`, and `Grew mark stack to ... frames`.
          NIX_CFLAGS_COMPILE = toString (
            [
              "-DINITIAL_MARK_STACK_SIZE=1048576"
              "-DGC_MANY_BLOCKS_DEFAULT=64"
            ]
            # For some reason that is not clear, it is wanting to use libgcc_eh which is not available.
            # Force this to be built with compiler-rt & libunwind over libgcc_eh works.
            # Issue: https://github.com/NixOS/nixpkgs/issues/177129
            ++
              lib.optionals
                (
                  stdenv.cc.isClang
                  && stdenv.hostPlatform.isStatic
                  && stdenv.cc.libcxx != null
                  && stdenv.cc.libcxx.isLLVM
                )
                [
                  "-rtlib=compiler-rt"
                  "-unwindlib=libunwind"
                ]
          );
        };

        buildInputs =
          (attrs.buildInputs or [ ])
          ++ lib.optional (
            stdenv.cc.isClang
            && stdenv.hostPlatform.isStatic
            && stdenv.cc.libcxx != null
            && stdenv.cc.libcxx.isLLVM
          ) pkgs.llvmPackages.libunwind;
      });

  lowdown =
    if lib.versionAtLeast pkgs.lowdown.version "2.0.2" then
      pkgs.lowdown
    else
      pkgs.lowdown.overrideAttrs (prevAttrs: rec {
        version = "2.0.2";
        src = pkgs.fetchurl {
          url = "https://kristaps.bsd.lv/lowdown/snapshots/lowdown-${version}.tar.gz";
          hash = "sha512-cfzhuF4EnGmLJf5EGSIbWqJItY3npbRSALm+GarZ7SMU7Hr1xw0gtBFMpOdi5PBar4TgtvbnG4oRPh+COINGlA==";
        };
        nativeBuildInputs = prevAttrs.nativeBuildInputs ++ [ pkgs.buildPackages.bmake ];
        postInstall =
          lib.replaceStrings [ "lowdown.so.1" "lowdown.1.dylib" ] [ "lowdown.so.2" "lowdown.2.dylib" ]
            (prevAttrs.postInstall or "");
      });

  curl =
    (pkgs.curl.override {
      http3Support = !pkgs.stdenv.hostPlatform.isWindows;
      # Make sure we enable all the dependencies for Content-Encoding/Transfer-Encoding decompression.
      zstdSupport = true;
      brotliSupport = true;
      zlibSupport = true;
      # libpsl uses a data file needed at runtime, not useful for nix.
      pslSupport = !stdenv.hostPlatform.isStatic;
      idnSupport = !stdenv.hostPlatform.isStatic;
    }).overrideAttrs
      {
        # TODO: Fix in nixpkgs. Static build with brotli is marked as broken, but it's not the case.
        # Remove once https://github.com/NixOS/nixpkgs/pull/494111 lands in the 25.11 channel.
        meta.broken = false;
      };

  libblake3 =
    (pkgs.libblake3.override {
      inherit stdenv;
      # Nixpkgs disables tbb on static
      useTBB = !(stdenv.hostPlatform.isWindows || stdenv.hostPlatform.isStatic);
    })
    # For some reason that is not clear, it is wanting to use libgcc_eh which is not available.
    # Force this to be built with compiler-rt & libunwind over libgcc_eh works.
    # Issue: https://github.com/NixOS/nixpkgs/issues/177129
    .overrideAttrs
      (
        attrs:
        lib.optionalAttrs
          (
            stdenv.cc.isClang
            && stdenv.hostPlatform.isStatic
            && stdenv.cc.libcxx != null
            && stdenv.cc.libcxx.isLLVM
          )
          {
            NIX_CFLAGS_COMPILE = [
              "-rtlib=compiler-rt"
              "-unwindlib=libunwind"
            ];

            buildInputs = [
              pkgs.llvmPackages.libunwind
            ];
          }
      );

  # TODO Hack until https://github.com/NixOS/nixpkgs/issues/45462 is fixed.
  boost =
    (pkgs.boost.override {
      extraB2Args = [
        "--with-container"
        "--with-context"
        "--with-coroutine"
        "--with-iostreams"
        "--with-url"
        "--with-thread"
      ];
      enableIcu = false;
      inherit stdenv;
    }).overrideAttrs
      (old: {
        # Need to remove `--with-*` to use `--with-libraries=...`
        buildPhase = lib.replaceStrings [ "--without-python" ] [ "" ] old.buildPhase;
        installPhase = lib.replaceStrings [ "--without-python" ] [ "" ] old.installPhase;
      });

  wasmtime = pkgs.callPackage ./wasmtime.nix { };

  sentry-native = (pkgs.callPackage ./sentry-native.nix { }).override {
    # Avoid having two curls in our closure.
    inherit (scope) curl;
  };

  libmicrohttpd = pkgs.libmicrohttpd.overrideDerivation (old: {
    # Don't pull in gnutls since it's pretty big and we don't need it.
    configureFlags = old.configureFlags or [ ] ++ [ "--without-gnutls" ];

    # Required for configuration detection for getsockname (for automatic port allocation for `nix serve`)
    __darwinAllowLocalNetworking = true;
  });
}
