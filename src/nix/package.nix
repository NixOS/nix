{
  stdenv,
  lib,
  mkMesonExecutable,

  nix-store,
  nix-expr,
  nix-main,
  nix-cmd,

  mimalloc,

  # Configuration Options

  version,

  # Whether to link against mimalloc for malloc override.
  # Significantly improves evaluation performance on allocation-heavy
  # workloads (~10-15% on large evaluations).
  withMimalloc ? !stdenv.hostPlatform.isWindows,

  # Whether to embed the public C API into the `nix` executable so plugins can
  # resolve those symbols without linking Nix libraries directly.
  withPluginCApi ? false,
}:

let
  inherit (lib) fileset;
in

mkMesonExecutable (finalAttrs: {
  pname = "nix";
  inherit version;

  workDir = ./.;
  fileset = fileset.unions (
    [
      ../../nix-meson-build-support
      ./nix-meson-build-support
      ../../.version
      ./.version
      ./meson.build
      ./meson.options

      # Symbolic links to other dirs
      ## exes
      ./doc
      ## dirs
      ./scripts
      ../../scripts
      ./misc
      ../../misc

      # Doc nix files for --help
      ../../doc/manual/generate-manpage.nix
      ../../doc/manual/utils.nix
      ../../doc/manual/generate-settings.nix
      ../../doc/manual/generate-store-info.nix

      # Other files to be included as string literals
      ./nix-channel/unpack-channel.nix
      ./nix-env/buildenv.nix
      ./get-env.sh
      ./help-stores.md
      ../../doc/manual/source/store/types/index.md.in
      ./profiles.md
      ../../doc/manual/source/command-ref/files/profiles.md

      # Files
    ]
    ++ [
      (fileset.fileFilter (file: file.hasExt "cc") ./.)
      (fileset.fileFilter (file: file.hasExt "hh") ./.)
      (fileset.fileFilter (file: file.hasExt "md") ./.)
    ]
    ++ lib.optionals withPluginCApi [
      (fileset.fileFilter (file: file.hasExt "cc") ../libutil-c)
      (fileset.fileFilter (file: file.hasExt "h") ../libutil-c)
      (fileset.fileFilter (file: file.hasExt "hh") ../libutil-c)
      (fileset.fileFilter (file: file.hasExt "cc") ../libstore-c)
      (fileset.fileFilter (file: file.hasExt "h") ../libstore-c)
      (fileset.fileFilter (file: file.hasExt "hh") ../libstore-c)
      (fileset.fileFilter (file: file.hasExt "cc") ../libfetchers-c)
      (fileset.fileFilter (file: file.hasExt "h") ../libfetchers-c)
      (fileset.fileFilter (file: file.hasExt "hh") ../libfetchers-c)
      (fileset.fileFilter (file: file.hasExt "cc") ../libexpr-c)
      (fileset.fileFilter (file: file.hasExt "h") ../libexpr-c)
      (fileset.fileFilter (file: file.hasExt "hh") ../libexpr-c)
      (fileset.fileFilter (file: file.hasExt "cc") ../libflake-c)
      (fileset.fileFilter (file: file.hasExt "h") ../libflake-c)
      (fileset.fileFilter (file: file.hasExt "hh") ../libflake-c)
      (fileset.fileFilter (file: file.hasExt "cc") ../libmain-c)
      (fileset.fileFilter (file: file.hasExt "h") ../libmain-c)
      (fileset.fileFilter (file: file.hasExt "hh") ../libmain-c)
    ]
  );

  buildInputs = [
    nix-store
    nix-expr
    nix-main
    nix-cmd
  ]
  ++ lib.optional withMimalloc mimalloc;

  mesonFlags = [
    (lib.mesonEnable "mimalloc" withMimalloc)
    (lib.mesonBool "plugin-c-api" withPluginCApi)
  ];

  postInstall = lib.optionalString stdenv.hostPlatform.isStatic ''
    mkdir -p $out/nix-support
    echo "file binary-dist $out/bin/nix" >> $out/nix-support/hydra-build-products
  '';

  passthru = {
    exportsPluginCApi = withPluginCApi;
  };

  meta = {
    mainProgram = "nix";
    platforms = lib.platforms.unix ++ lib.platforms.windows;
  };

})
