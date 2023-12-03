{ lib
, callPackage
, stdenv
, releaseTools
, versionSuffix ? ""
, officialRelease ? false
, buildUnreleasedNotes ? false
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
#
# This probably seems like too many degrees of freedom, but it
# faithfully reflects how the underlying configure + make build system
# work. The top-level flake.nix will choose useful combinations.

, pname ? "nix"

, doBuild ? true
, doCheck ? __forDefaults.canRunInstalled
, doInstallCheck ? __forDefaults.canRunInstalled

, withCoverageChecks ? false

# Whether to build the regular manual
, enableManual ? __forDefaults.canRunInstalled
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
} @ attrs0:

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
    then releaseTools.coverageAnalysis
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
        ] ++ lib.optionals doBuild [
          ./boehmgc-coroutine-sp-fallback.diff
          ./doc
          ./misc
          ./precompiled-headers.h
          ./src
          ./unit-test-data
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
    bison
    flex
    (lib.getBin lowdown)
    jq # Also for custom mdBook preprocessor.
    mdbook
    mdbook-linkcheck
    autoconf-archive
    autoreconfHook
    pkg-config
  ]
  ++ lib.optional stdenv.hostPlatform.isLinux util-linux
  # Official releases don't have rl-next, so we don't need to compile a
  # changelog
  ++ lib.optional (!officialRelease && buildUnreleasedNotes) changelog-d;

  buildInputs = lib.optionals doBuild [
    boost
    brotli
    bzip2
    curl
    editline
    libarchive
    libgit2
    libsodium
    lowdown
    openssl
    sqlite
    xz
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
  ] ++ lib.optionals enableInternalAPIDocs [
    doxygen
  ];

  disallowedReferences = [ boost ];

  preConfigure = lib.optionalString (! stdenv.hostPlatform.isStatic) ''
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
    (lib.enableFeature anySortOfTesting "test")
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
      install_name_tool \
      -change ${boost}/lib/libboost_regex.dylib \
      $out/lib/libboost_regex.dylib \
      $out/lib/libnixexpr.dylib
    ''
  ) + lib.optionalString enableInternalAPIDocs ''
    mkdir -p ''${!outputDoc}/nix-support
    echo "doc internal-api-docs $out/share/doc/nix/internal-api/html" >> ''${!outputDoc}/nix-support/hydra-build-products
  '';

  doInstallCheck = attrs.doInstallCheck;

  installCheckFlags = "sysconfdir=$(out)/etc";
  installCheckTarget = "installcheck"; # work around buggy detection in stdenv

  # Needed for tests if we are not doing a build, but testing existing
  # built Nix.
  preInstallCheck = lib.optionalString (! doBuild) ''
    mkdir -p src/nix-channel
  '';

  separateDebugInfo = !stdenv.hostPlatform.isStatic;

  strictDeps = true;

  hardeningDisable = lib.optional stdenv.hostPlatform.isStatic "pie";

  passthru = {
    inherit filesets;

    perl-bindings = callPackage ./perl {
      inherit fileset stdenv;
    };
  };

  meta = {
    platforms = lib.platforms.unix;
    mainProgram = "nix";
    broken = !(lib.all (a: a) [
      (installUnitTests -> doBuild)
      (doCheck -> doBuild)
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
