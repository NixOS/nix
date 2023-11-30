{ lib
, callPackage
, stdenv
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
, sh
, sqlite
, util-linux
, xz
}:

let

  version = lib.fileContents ./.version + versionSuffix;

  inherit (stdenv.hostPlatform) isStatic;

  canRunInstalled = stdenv.buildPlatform.canExecute stdenv.hostPlatform;
in

stdenv.mkDerivation (finalAttrs: {
  name = "nix-${version}";
  inherit version;

  src =
    let
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

    in
      fileset.toSource {
        root = ./.;
        fileset = fileset.intersect baseFiles (fileset.unions [
          configureFiles
          topLevelBuildFiles
          ./boehmgc-coroutine-sp-fallback.diff
          ./doc
          ./misc
          ./precompiled-headers.h
          ./src
          ./unit-test-data
          ./COPYING
          ./scripts/local.mk
          functionalTestFiles
        ]);
      };

  VERSION_SUFFIX = versionSuffix;

  outputs = [ "out" "dev" "doc" ]
    ++ lib.optional (stdenv.hostPlatform != stdenv.buildPlatform) "check";

  nativeBuildInputs = [
    bison
    flex
    (lib.getBin lowdown)
    mdbook
    mdbook-linkcheck
    autoconf-archive
    autoreconfHook
    pkg-config

    # Tests
    git
    mercurial # FIXME: remove? only needed for tests
    jq # Also for custom mdBook preprocessor.
    openssh # only needed for tests (ssh-keygen)
  ]
  ++ lib.optional stdenv.hostPlatform.isLinux util-linux
  # Official releases don't have rl-next, so we don't need to compile a changelog
  ++ lib.optional (!officialRelease && buildUnreleasedNotes) changelog-d;

  buildInputs = [
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
  ]
  ++ lib.optionals stdenv.isLinux [libseccomp]
  ++ lib.optional stdenv.hostPlatform.isx86_64 libcpuid
  # There have been issues building these dependencies
  ++ lib.optionals (stdenv.hostPlatform == stdenv.buildPlatform) (lib.optional (stdenv.isLinux || stdenv.isDarwin)
    (aws-sdk-cpp.override {
      apis = ["s3" "transfer"];
      customMemoryManagement = false;
    }))
  ++ lib.optionals finalAttrs.doCheck ([
    gtest
    rapidcheck
  ]);

  propagatedBuildInputs = [
    boehmgc
    nlohmann_json
  ];

  disallowedReferences = [ boost ];

  preConfigure = lib.optionalString (! stdenv.hostPlatform.isStatic)
    ''
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

  configureFlags =
    lib.optionals stdenv.isLinux [
      "--with-boost=${boost}/lib"
      "--with-sandbox-shell=${sh}/bin/busybox"
    ]
    ++ lib.optionals (stdenv.isLinux && !(isStatic && stdenv.system == "aarch64-linux")) [
      "LDFLAGS=-fuse-ld=gold"
    ]
    ++ [ "--sysconfdir=/etc" ]
    ++ lib.optional stdenv.hostPlatform.isStatic "--enable-embedded-sandbox-shell"
    ++ [ (lib.enableFeature finalAttrs.doCheck "tests") ]
    ++ lib.optionals finalAttrs.doCheck ([ "RAPIDCHECK_HEADERS=${lib.getDev rapidcheck}/extras/gtest/include" ]
    ++ lib.optionals (stdenv.hostPlatform != stdenv.buildPlatform) [
      "--enable-install-unit-tests"
      "--with-check-bin-dir=${builtins.placeholder "check"}/bin"
      "--with-check-lib-dir=${builtins.placeholder "check"}/lib"
    ])
    ++ lib.optional (!canRunInstalled) "--disable-doc-gen";

  enableParallelBuilding = true;

  makeFlags = "profiledir=$(out)/etc/profile.d PRECOMPILE_HEADERS=1";

  doCheck = true;

  installFlags = "sysconfdir=$(out)/etc";

  postInstall = ''
    mkdir -p $doc/nix-support
    echo "doc manual $doc/share/doc/nix/manual" >> $doc/nix-support/hydra-build-products
    ${lib.optionalString stdenv.hostPlatform.isStatic ''
      mkdir -p $out/nix-support
      echo "file binary-dist $out/bin/nix" >> $out/nix-support/hydra-build-products
    ''}
    ${lib.optionalString stdenv.isDarwin ''
      install_name_tool \
      -change ${boost}/lib/libboost_context.dylib \
      $out/lib/libboost_context.dylib \
      $out/lib/libnixutil.dylib
      install_name_tool \
      -change ${boost}/lib/libboost_regex.dylib \
      $out/lib/libboost_regex.dylib \
      $out/lib/libnixexpr.dylib
    ''}
  '';

  doInstallCheck = finalAttrs.doCheck;
  installCheckFlags = "sysconfdir=$(out)/etc";
  installCheckTarget = "installcheck"; # work around buggy detection in stdenv

  separateDebugInfo = !stdenv.hostPlatform.isStatic;

  strictDeps = true;

  hardeningDisable = lib.optional stdenv.hostPlatform.isStatic "pie";

  passthru.perl-bindings = callPackage ./perl {
    inherit fileset stdenv;
  };

  meta.platforms = lib.platforms.unix;
  meta.mainProgram = "nix";
})
