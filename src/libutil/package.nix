{ lib
, stdenv
, releaseTools

, meson
, ninja
, pkg-config

, boost
, brotli
, libarchive
, libcpuid
, libsodium
, nlohmann_json
, openssl

# Configuration Options

, versionSuffix ? ""

# Check test coverage of Nix. Probably want to use with at least
# one of `doCheck` or `doInstallCheck` enabled.
, withCoverageChecks ? false
}:

let
  inherit (lib) fileset;

  version = lib.fileContents ./.version + versionSuffix;

  mkDerivation =
    if withCoverageChecks
    then
      # TODO support `finalAttrs` args function in
      # `releaseTools.coverageAnalysis`.
      argsFun:
         releaseTools.coverageAnalysis (let args = argsFun args; in args)
    else stdenv.mkDerivation;
in

mkDerivation (finalAttrs: {
  pname = "nix-util";
  inherit version;

  src = fileset.toSource {
    root = ./.;
    fileset = fileset.unions [
      ./meson.build
      ./meson.options
      ./linux/meson.build
      ./unix/meson.build
      ./windows/meson.build
      (fileset.fileFilter (file: file.hasExt "cc") ./.)
      (fileset.fileFilter (file: file.hasExt "hh") ./.)
    ];
  };

  outputs = [ "out" "dev" ];

  nativeBuildInputs = [
    meson
    ninja
    pkg-config
  ];

  buildInputs = [
    boost
    brotli
    libsodium
    openssl
  ] ++ lib.optional stdenv.hostPlatform.isx86_64 libcpuid
  ;

  propagatedBuildInputs = [
    boost.dev
    libarchive
    nlohmann_json
  ];

  disallowedReferences = [ boost ];

  preConfigure =
    # "Inline" .version so it's not a symlink, and includes the suffix
    ''
      echo ${version} > .version
    ''
    # Copy some boost libraries so we don't get all of Boost in our
    # closure. https://github.com/NixOS/nixpkgs/issues/45462
    + lib.optionalString (!stdenv.hostPlatform.isStatic) (''
      mkdir -p $out/lib
      cp -pd ${boost}/lib/{libboost_context*,libboost_thread*,libboost_system*} $out/lib
      rm -f $out/lib/*.a
    '' + lib.optionalString stdenv.hostPlatform.isLinux ''
      chmod u+w $out/lib/*.so.*
      patchelf --set-rpath $out/lib:${stdenv.cc.cc.lib}/lib $out/lib/libboost_thread.so.*
    '' + lib.optionalString stdenv.hostPlatform.isDarwin ''
      for LIB in $out/lib/*.dylib; do
        chmod u+w $LIB
        install_name_tool -id $LIB $LIB
        install_name_tool -delete_rpath ${boost}/lib/ $LIB || true
      done
      install_name_tool -change ${boost}/lib/libboost_system.dylib $out/lib/libboost_system.dylib $out/lib/libboost_thread.dylib
    ''
  );

  mesonFlags = [
    (lib.mesonEnable "cpuid" stdenv.hostPlatform.isx86_64)
  ];

  env = {
    # Needed for Meson to find Boost.
    # https://github.com/NixOS/nixpkgs/issues/86131.
    BOOST_INCLUDEDIR = "${lib.getDev boost}/include";
    BOOST_LIBRARYDIR = "${lib.getLib boost}/lib";
  } // lib.optionalAttrs (stdenv.isLinux && !(stdenv.hostPlatform.isStatic && stdenv.system == "aarch64-linux")) {
    LDFLAGS = "-fuse-ld=gold";
  };

  enableParallelBuilding = true;

  postInstall =
    # Remove absolute path to boost libs that ends up in `Libs.private`
    # by default, and would clash with out `disallowedReferences`. Part
    # of the https://github.com/NixOS/nixpkgs/issues/45462 workaround.
    ''
      sed -i "$out/lib/pkgconfig/nix-util.pc" -e 's, ${lib.getLib boost}[^ ]*,,g'
    ''
    + lib.optionalString stdenv.isDarwin ''
      install_name_tool \
      -change ${boost}/lib/libboost_context.dylib \
      $out/lib/libboost_context.dylib \
      $out/lib/libnixutil.dylib
    '';

  separateDebugInfo = !stdenv.hostPlatform.isStatic;

  # TODO Always true after https://github.com/NixOS/nixpkgs/issues/318564
  strictDeps = !withCoverageChecks;

  hardeningDisable = lib.optional stdenv.hostPlatform.isStatic "pie";

  meta = {
    platforms = lib.platforms.unix ++ lib.platforms.windows;
  };

} // lib.optionalAttrs withCoverageChecks {
  lcovFilter = [ "*/boost/*" "*-tab.*" ];

  hardeningDisable = [ "fortify" ];
})
