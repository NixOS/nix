{
  gccStdenv,
  fetchFromGitHub,
  cmake,
  gtest,
  zlib,
  copyPkgconfigItems,
  makePkgconfigItem,
  lib,
  testers,
  ctestCheckHook,
}:
# Force gccStdenv otherwise it might try to use ccachStdenv
# from .envrc and fail to build
# This can go away when the nixpkgs pull request is merged.
# https://github.com/NixOS/nixpkgs/pull/454243
gccStdenv.mkDerivation (finalAttrs: {
  pname = "kaitai-struct-cpp-stl-runtime";
  version = "0.11";

  src = fetchFromGitHub {
    owner = "kaitai-io";
    repo = "kaitai_struct_cpp_stl_runtime";
    tag = finalAttrs.version;
    hash = "sha256-2glGPf08bkzvnkLpQIaG2qiy/yO+bZ14hjIaCKou2vU=";
  };

  doCheck = true;

  nativeBuildInputs = [
    cmake
    copyPkgconfigItems
    ctestCheckHook
  ];

  buildInputs = [
    zlib.dev
    gtest
  ];

  strictDeps = true;

  pkgconfigItems = [
    (makePkgconfigItem rec {
      name = "kaitai-struct-cpp-stl-runtime";
      inherit (finalAttrs) version;
      cflags = [ "-I${variables.includedir}" ];
      libs = [
        "-L${variables.libdir}"
        "-lkaitai_struct_cpp_stl_runtime"
      ];
      variables = rec {
        prefix = placeholder "out";
        includedir = "${prefix}/include";
        libdir = "${prefix}/lib";
      };
      inherit (finalAttrs.meta) description;
    })
  ];

  passthru = {
    tests.pkg-config = testers.testMetaPkgConfig finalAttrs.finalPackage;
  };

  meta = {
    pkgConfigModules = [ "kaitai-struct-cpp-stl-runtime" ];
    description = "Kaitai Struct C++ STL Runtime Library";
    homepage = "https://github.com/kaitai-io/kaitai_struct_cpp_stl_runtime";
    license = lib.licenses.mit;
    maintainers = with lib.maintainers; [ fzakaria ];
  };
})
