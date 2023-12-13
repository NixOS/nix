{ lib
, stdenv
, releaseTools
, autoconf-archive
, autoreconfHook
, aws-sdk-cpp
, boehmgc
, nlohmann_json
, bison
, boost
, brotli
, bzip2
, changelog-d
, curl
, editline
, fileset
, flex
, git
, gtest
, jq
, doxygen
, libarchive
, libcpuid
, libgit2
, libseccomp
, libsodium
, lowdown
, mdbook
, mdbook-linkcheck
, mercurial
, openssh
, openssl
, pkg-config
, rapidcheck
, sqlite
, util-linux
, xz

, busybox-sandbox-shell ? null

# Configuration Options
#:
# This probably seems like too many degrees of freedom, but it
# faithfully reflects how the underlying configure + make build system
# work. The top-level flake.nix will choose useful combinations of these
# options to CI.

, pname ? "nix"

, versionSuffix ? ""
, officialRelease ? false

# Whether to build Nix. Useful to skip for tasks like (a) just
# generating API docs or (b) testing existing pre-built versions of Nix
, doBuild ? true

# Run the unit tests as part of the build. See `installUnitTests` for an
# alternative to this.
, doCheck ? __forDefaults.canRunInstalled

# Run the functional tests as part of the build.
, doInstallCheck ? test-client != null || __forDefaults.canRunInstalled

# Check test coverage of Nix. Probably want to use with with at least
# one of `doCHeck` or `doInstallCheck` enabled.
, withCoverageChecks ? false

# Whether to build the regular manual
, enableManual ? __forDefaults.canRunInstalled

# Whether to compile `rl-next.md`, the release notes for the next
# not-yet-released version of Nix in the manul, from the individual
# change log entries in the directory.
, buildUnreleasedNotes ? false

# Whether to build the internal API docs, can be done separately from
# everything else.
, enableInternalAPIDocs ? false

# Whether to install unit tests. This is useful when cross compiling
# since we cannot run them natively during the build, but can do so
# later.
, installUnitTests ? __forDefaults.canRunInstalled

# For running the functional tests against a pre-built Nix. Probably
# want to use in conjunction with `doBuild = false;`.
, test-daemon ? null
, test-client ? null

# Not a real argument, just the only way to approximate let-binding some
# stuff for argument defaults.
, __forDefaults ? {
    canRunInstalled = doBuild && stdenv.buildPlatform.canExecute stdenv.hostPlatform;
  }
}:

let
  version = lib.fileContents ./.version + versionSuffix;

  # selected attributes with defaults, will be used to define some
  # things which should instead be gotten via `finalAttrs` in order to
  # work with overriding.
  attrs = {
    inherit doBuild doCheck doInstallCheck;
  };

  filesets = {
    baseFiles = fileset.fileFilter (f: f.name != ".gitignore") ./.;

    configureFiles = fileset.unions [
      ./.version
      ./configure.ac
      ./m4
      # TODO: do we really need README.md? It doesn't seem used in the build.
      ./README.md
    ];

    topLevelBuildFiles = fileset.unions [
      ./local.mk
      ./Makefile
      ./Makefile.config.in
      ./mk
    ];

    functionalTestFiles = fileset.unions [
      ./tests/functional
      (fileset.fileFilter (f: lib.strings.hasPrefix "nix-profile" f.name) ./scripts)
    ];
  };

  mkDerivation =
    if withCoverageChecks
    then
      # TODO support `finalAttrs` args function in
      # `releaseTools.coverageAnalysis`.
      argsFun:
         releaseTools.coverageAnalysis (let args = argsFun args; in args)
    else stdenv.mkDerivation;
in

mkDerivation (finalAttrs: let

  inherit (finalAttrs)
    doCheck
    doInstallCheck
    ;

  doBuild = !finalAttrs.dontBuild;

  # Either running the unit tests during the build, or installing them
  # to be run later, requiresthe unit tests to be built.
  buildUnitTests = doCheck || installUnitTests;

  anySortOfTesting = buildUnitTests || doInstallCheck;

in {
  inherit pname version;

  src =
    let

    in
      fileset.toSource {
        root = ./.;
        fileset = fileset.intersect filesets.baseFiles (fileset.unions ([
          filesets.configureFiles
          filesets.topLevelBuildFiles
          ./doc/internal-api
        ] ++ lib.optionals doBuild [
          ./boehmgc-coroutine-sp-fallback.diff
          ./doc
          ./misc
          ./precompiled-headers.h
          ./src
          ./tests/unit
          ./COPYING
          ./scripts/local.mk
        ] ++ lib.optionals anySortOfTesting [
          filesets.functionalTestFiles
        ]));
      };

  VERSION_SUFFIX = versionSuffix;

  outputs = [ "out" ]
    ++ lib.optional doBuild "dev"
    # If we are doing just build or just docs, the one thing will use
    # "out". We only need additional outputs if we are doing both.
    ++ lib.optional (doBuild && (enableManual || enableInternalAPIDocs)) "doc"
    ++ lib.optional installUnitTests "check";

  nativeBuildInputs = [
    autoconf-archive
    autoreconfHook
    pkg-config
  ] ++ lib.optionals doBuild [
    bison
    flex
  ] ++ lib.optionals enableManual [
    (lib.getBin lowdown)
    mdbook
    mdbook-linkcheck
  ] ++ lib.optionals (doInstallCheck || enableManual) [
    jq # Also for custom mdBook preprocessor.
  ] ++ lib.optional stdenv.hostPlatform.isLinux util-linux
    # Official releases don't have rl-next, so we don't need to compile a
    # changelog
    ++ lib.optional (!officialRelease && buildUnreleasedNotes) changelog-d
    ++ lib.optional enableInternalAPIDocs doxygen
  ;

  buildInputs = lib.optionals doBuild [
    boost
    brotli
    bzip2
    curl
    libarchive
    libgit2
    libsodium
    openssl
    sqlite
    xz
  ] ++ lib.optionals (!stdenv.hostPlatform.isWindows) [
    editline
    lowdown
  ] ++ lib.optional stdenv.isLinux libseccomp
    ++ lib.optional stdenv.hostPlatform.isx86_64 libcpuid
    # There have been issues building these dependencies
    ++ lib.optional (stdenv.hostPlatform == stdenv.buildPlatform && (stdenv.isLinux || stdenv.isDarwin))
      (aws-sdk-cpp.override {
        apis = ["s3" "transfer"];
        customMemoryManagement = false;
      })
  ;

  propagatedBuildInputs = [
    boehmgc
    nlohmann_json
  ];

  dontBuild = !attrs.doBuild;
  doCheck = attrs.doCheck;

  checkInputs = [
    gtest
    rapidcheck
  ];

  nativeCheckInputs = [
    git
    mercurial
    openssh
  ];

  disallowedReferences = [ boost ];

  preConfigure = lib.optionalString (doBuild && ! stdenv.hostPlatform.isStatic) ''
    # Copy libboost_context so we don't get all of Boost in our closure.
    # https://github.com/NixOS/nixpkgs/issues/45462
    mkdir -p $out/lib
    cp -pd ${boost}/lib/{libboost_context*,libboost_thread*,libboost_system*,libboost_regex*} $out/lib
    rm -f $out/lib/*.a
    ${lib.optionalString stdenv.hostPlatform.isLinux ''
      chmod u+w $out/lib/*.so.*
      patchelf --set-rpath $out/lib:${stdenv.cc.cc.lib}/lib $out/lib/libboost_thread.so.*
    ''}
    ${lib.optionalString stdenv.hostPlatform.isDarwin ''
      for LIB in $out/lib/*.dylib; do
        chmod u+w $LIB
        install_name_tool -id $LIB $LIB
        install_name_tool -delete_rpath ${boost}/lib/ $LIB || true
      done
      install_name_tool -change ${boost}/lib/libboost_system.dylib $out/lib/libboost_system.dylib $out/lib/libboost_thread.dylib
    ''}
  '';

  configureFlags = [
    "--sysconfdir=/etc"
    (lib.enableFeature doBuild "build")
    (lib.enableFeature anySortOfTesting "tests")
    (lib.enableFeature enableInternalAPIDocs "internal-api-docs")
    (lib.enableFeature enableManual "doc-gen")
    (lib.enableFeature installUnitTests "install-unit-tests")
  ] ++ lib.optionals installUnitTests [
    "--with-check-bin-dir=${builtins.placeholder "check"}/bin"
    "--with-check-lib-dir=${builtins.placeholder "check"}/lib"
  ] ++ lib.optionals (doBuild && stdenv.isLinux) [
    "--with-boost=${boost}/lib"
    "--with-sandbox-shell=${busybox-sandbox-shell}/bin/busybox"
  ] ++ lib.optional (doBuild && stdenv.isLinux && !(stdenv.hostPlatform.isStatic && stdenv.system == "aarch64-linux"))
       "LDFLAGS=-fuse-ld=gold"
    ++ lib.optional (doBuild && stdenv.hostPlatform.isStatic) "--enable-embedded-sandbox-shell"
    ++ lib.optional buildUnitTests "RAPIDCHECK_HEADERS=${lib.getDev rapidcheck}/extras/gtest/include";

  enableParallelBuilding = true;

  makeFlags = "profiledir=$(out)/etc/profile.d PRECOMPILE_HEADERS=1";

  installTargets = lib.optional doBuild "install"
    ++ lib.optional enableInternalAPIDocs "internal-api-html";

  installFlags = "sysconfdir=$(out)/etc";

  # In this case we are probably just running tests, and so there isn't
  # anything to install, we just make an empty directory to signify tests
  # succeeded.
  installPhase = if finalAttrs.installTargets != [] then null else ''
    mkdir -p $out
  '';

  postInstall = lib.optionalString doBuild (
    ''
      mkdir -p $doc/nix-support
      echo "doc manual $doc/share/doc/nix/manual" >> $doc/nix-support/hydra-build-products
    '' + lib.optionalString stdenv.hostPlatform.isStatic ''
      mkdir -p $out/nix-support
      echo "file binary-dist $out/bin/nix" >> $out/nix-support/hydra-build-products
    '' + lib.optionalString stdenv.isDarwin ''
      install_name_tool \
      -change ${boost}/lib/libboost_context.dylib \
      $out/lib/libboost_context.dylib \
      $out/lib/libnixutil.dylib
    ''
  ) + lib.optionalString enableInternalAPIDocs ''
    mkdir -p ''${!outputDoc}/nix-support
    echo "doc internal-api-docs $out/share/doc/nix/internal-api/html" >> ''${!outputDoc}/nix-support/hydra-build-products
  '';

  doInstallCheck = attrs.doInstallCheck;

  installCheckFlags = "sysconfdir=$(out)/etc";
  # Work around buggy detection in stdenv.
  installCheckTarget = "installcheck";

  # Work around weird bug where it doesn't think there is a Makefile.
  installCheckPhase = if (!doBuild && doInstallCheck) then ''
    mkdir -p src/nix-channel
    make installcheck -j$NIX_BUILD_CORES -l$NIX_BUILD_CORES
  '' else null;

  # Needed for tests if we are not doing a build, but testing existing
  # built Nix.
  preInstallCheck = lib.optionalString (! doBuild) ''
    mkdir -p src/nix-channel
  '';

  separateDebugInfo = !stdenv.hostPlatform.isStatic;

  # TODO `releaseTools.coverageAnalysis` in Nixpkgs needs to be updated
  # to work with `strictDeps`.
  strictDeps = !withCoverageChecks;

  hardeningDisable = lib.optional stdenv.hostPlatform.isStatic "pie";

  meta = {
    platforms = lib.platforms.unix ++ lib.platforms.windows;
    mainProgram = "nix";
    broken = !(lib.all (a: a) [
      # We cannot run or install unit tests if we don't build them or
      # Nix proper (which they depend on).
      (installUnitTests -> doBuild)
      (doCheck -> doBuild)
      # We have to build the manual to build unreleased notes, as those
      # are part of the manual
      (buildUnreleasedNotes -> enableManual)
      # The build process for the manual currently requires extracting
      # data from the Nix executable we are trying to document.
      (enableManual -> doBuild)
    ]);
  };

} // lib.optionalAttrs withCoverageChecks {
  lcovFilter = [ "*/boost/*" "*-tab.*" ];

  hardeningDisable = ["fortify"];

  NIX_CFLAGS_COMPILE = "-DCOVERAGE=1";

  dontInstall = false;
} // lib.optionalAttrs (test-daemon != null) {
  NIX_DAEMON_PACKAGE = test-daemon;
} // lib.optionalAttrs (test-client != null) {
  NIX_CLIENT_PACKAGE = test-client;
})
