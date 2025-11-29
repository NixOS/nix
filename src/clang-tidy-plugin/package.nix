{
  lib,
  stdenv,
  mkMesonDerivation,

  pkg-config,
  llvmPackages,

  # Configuration Options

  version,
}:

let
  inherit (lib) fileset;
in

mkMesonDerivation (finalAttrs: {
  pname = "nix-clang-tidy-plugin";
  inherit version;

  workDir = ./.;
  fileset = fileset.unions [
    ../../nix-meson-build-support
    ./nix-meson-build-support
    ../../.version
    ./.version
    ./meson.build
    (fileset.fileFilter (file: file.hasExt "cc") ./.)
    (fileset.fileFilter (file: file.hasExt "hh") ./.)
  ];

  nativeBuildInputs = [
    pkg-config
    llvmPackages.llvm.dev
  ];

  buildInputs = [
    llvmPackages.libclang
    llvmPackages.clang-unwrapped.dev # for ClangTidyModule.h
  ];

  meta = {
    description = "Custom clang-tidy checks for the Nix codebase";
    platforms = lib.platforms.unix;
    broken = !stdenv.cc.isClang;
  };
})
