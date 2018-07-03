{ pkgs }:

with pkgs;

let

  cpptoml = runCommand "cpptoml"
    { src = fetchFromGitHub {
        owner = "skystrife";
        repo = "cpptoml";
        rev = "43d7d8e52de149fd84aedf7eb71814ff9e6b2f7e";
        sha256 = "0gdxk1mj5hpj93df1kwfl903w06nihbb1ayr3x336775jm2d2cw6";
      };
    }
    ''
      mkdir -p $out/include
      cp $src/include/cpptoml.h $out/include/
    '';

in

rec {
  # Use "busybox-sandbox-shell" if present,
  # if not (legacy) fallback and hope it's sufficient.
  sh = pkgs.busybox-sandbox-shell or (busybox.override {
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

  configureFlags =
    [ "--disable-init-state"
      "--enable-gc"
    ] ++ lib.optionals stdenv.isLinux [
      "--with-sandbox-shell=${sh}/bin/busybox"
    ];

  tarballDeps =
    [ bison
      flex
      libxml2
      libxslt
      docbook5
      docbook5_xsl
      autoconf-archive
      autoreconfHook
    ];

  buildDeps =
    [ curl
      bzip2 xz brotli
      openssl pkgconfig sqlite boehmgc
      boost
      cpptoml

      # Tests
      git
      mercurial
    ]
    ++ lib.optionals stdenv.isLinux [libseccomp utillinuxMinimal]
    ++ lib.optional (stdenv.isLinux || stdenv.isDarwin) libsodium
    ++ lib.optional (stdenv.isLinux || stdenv.isDarwin)
      (aws-sdk-cpp.override {
        apis = ["s3" "transfer"];
        customMemoryManagement = false;
      });

  perlDeps =
    [ perl
      perlPackages.DBDSQLite
    ];
}
