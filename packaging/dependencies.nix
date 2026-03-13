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

  curl =
    (pkgs.curl.override {
      inherit stdenv;
      http3Support = !pkgs.stdenv.hostPlatform.isWindows;
      # Make sure we enable all the dependencies for Content-Encoding/Transfer-Encoding decompression.
      zstdSupport = true;
      brotliSupport = true;
      zlibSupport = true;
      # libpsl uses a data file needed at runtime, not useful for nix.
      pslSupport = !stdenv.hostPlatform.isStatic;
      idnSupport = !stdenv.hostPlatform.isStatic;
    }).overrideAttrs
      (attrs: {
        # For some reason that is not clear, it is wanting to use libgcc_eh which is not available.
        # Force this to be built with compiler-rt & libunwind over libgcc_eh works.
        # Issue: https://github.com/NixOS/nixpkgs/issues/177129
        NIX_CFLAGS_COMPILE =
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
            ];

        buildInputs =
          (attrs.buildInputs or [ ])
          ++ lib.optional (
            stdenv.cc.isClang
            && stdenv.hostPlatform.isStatic
            && stdenv.cc.libcxx != null
            && stdenv.cc.libcxx.isLLVM
          ) pkgs.llvmPackages.libunwind;

        # TODO: Fix in nixpkgs. Static build with brotli is marked as broken, but it's not the case.
        # Remove once https://github.com/NixOS/nixpkgs/pull/494111 lands in the 25.11 channel.
        meta.broken = false;
      });

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
      inherit stdenv;
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
