# Builds the nix-tidy clang-tidy plugin (.so) containing custom
# AST-matcher-based checks for the Nix codebase.
#
# The plugin is loaded at runtime via clang-tidy --load=<path>.
{
  pkgs,
  src,
}:

let
  llvmPkgs = pkgs.llvmPackages;
in

pkgs.stdenv.mkDerivation {
  pname = "nix-tidy-checks";
  version = "0.1.0";

  src = src + "/packaging/analysis/clang-tidy-checks";

  nativeBuildInputs = [ pkgs.cmake ];

  buildInputs = [
    llvmPkgs.clang-unwrapped.dev  # clang-tidy headers + ClangConfig.cmake
    llvmPkgs.clang-unwrapped.lib  # libclang-cpp.so
    llvmPkgs.llvm.dev             # LLVMConfig.cmake + LLVM headers
    llvmPkgs.llvm.lib             # LLVM shared libraries
  ];

  cmakeFlags = [
    "-DClang_DIR=${llvmPkgs.clang-unwrapped.dev}/lib/cmake/clang"
    "-DLLVM_DIR=${llvmPkgs.llvm.dev}/lib/cmake/llvm"
  ];

  installPhase = ''
    mkdir -p $out/lib
    cp libnix-tidy.so $out/lib/
  '';
}
