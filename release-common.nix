{ pkgs }:

let
  inherit (pkgs) stdenv lib curl;
  # TODO upstream
  mesonFlag = key: value: "-D${key}=${value}";
  mesonBool = feature: cond: mesonFlag feature (if cond then "true" else "false");
  mesonFeature = feature: cond: mesonFlag feature (if cond then "enabled" else "disabled");
in

rec {
  # Use "busybox-sandbox-shell" if present,
  # if not (legacy) fallback and hope it's sufficient.
  sh = pkgs.busybox-sandbox-shell or (pkgs.busybox.override {
    useMusl = true;
    enableStatic = true;
    enableMinimal = true;
    extraConfig = ''
      CONFIG_FEATURE_FANCY_ECHO y
      CONFIG_FEATURE_SH_MATH y
      CONFIG_FEATURE_SH_MATH_64 y

      CONFIG_ASH y
      CONFIG_ASH_OPTIMIZE_FOR_SIZE y

      CONFIG_ASH_ALIAS y
      CONFIG_ASH_BASH_COMPAT y
      CONFIG_ASH_CMDCMD y
      CONFIG_ASH_ECHO y
      CONFIG_ASH_GETOPTS y
      CONFIG_ASH_INTERNAL_GLOB y
      CONFIG_ASH_JOB_CONTROL y
      CONFIG_ASH_PRINTF y
      CONFIG_ASH_TEST y
    '';
  });

  mesonFlags = [
    (mesonBool "with_gc" true)
    (mesonFeature "with_libsodium" stdenv.hostPlatform.isLinux)
    (mesonFeature "with_editline" (!stdenv.hostPlatform.isWindows))
  ];

  configureFlags =
    [
      "--enable-gc"
    ] ++ lib.optionals stdenv.isLinux [
      "--with-sandbox-shell=${sh}/bin/busybox"
    ];

  tarballDeps = with pkgs.buildPackages;
    [ bison
      flex
      libxml2
      libxslt
      docbook5
      docbook_xsl_ns
      autoconf-archive
      autoreconfHook
    ];

  nativeBuildDeps = with pkgs.buildPackages; [
    pkgconfig
    meson
    ninja
  ];

  buildDeps = with pkgs; [
    bzip2
    xz
    brotli
    (if stdenv.hostPlatform.isWindows then openssl_1_0_2 else openssl)
    sqlite
    boehmgc
    boost
    curl

    # Tests
    git
    mercurial
  ] ++ lib.optionals (!stdenv.hostPlatform.isWindows) [
    editline
  ] ++ lib.optionals stdenv.isLinux [libseccomp utillinuxMinimal]
    ++ lib.optional (stdenv.isLinux || stdenv.isDarwin) libsodium
    ++ lib.optional (stdenv.isLinux || stdenv.isDarwin)
      ((aws-sdk-cpp.override {
        apis = ["s3" "transfer"];
        customMemoryManagement = false;
      }).overrideDerivation (args: {
        /*
        patches = args.patches or [] ++ [ (fetchpatch {
          url = https://github.com/edolstra/aws-sdk-cpp/commit/3e07e1f1aae41b4c8b340735ff9e8c735f0c063f.patch;
          sha256 = "1pij0v449p166f9l29x7ppzk8j7g9k9mp15ilh5qxp29c7fnvxy2";
        }) ];
        */
      }));

  perlDeps = with pkgs;
    [ perl
      perlPackages.DBDSQLite
    ];
}
