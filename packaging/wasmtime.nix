# Stripped-down version of https://github.com/NixOS/nixpkgs/blob/master/pkgs/by-name/wa/wasmtime/package.nix,
# license: https://github.com/NixOS/nixpkgs/blob/master/COPYING
{
  lib,
  stdenv,
  rust,
  fetchFromGitHub,
  cmake,
  enableShared ? !stdenv.hostPlatform.isStatic,
  enableStatic ? stdenv.hostPlatform.isStatic,
}:
rust.packages.stable.rustPlatform.buildRustPackage (finalAttrs: {
  pname = "wasmtime";
  version = "40.0.2";

  src = fetchFromGitHub {
    owner = "bytecodealliance";
    repo = "wasmtime";
    tag = "v${finalAttrs.version}";
    hash = "sha256-4y9WpCdyuF/Tp2k/1d5rZxwYunWNdeibEsFgHcBC52Q=";
    fetchSubmodules = true;
  };

  # Disable cargo-auditable until https://github.com/rust-secure-code/cargo-auditable/issues/124 is solved.
  auditable = false;

  cargoHash = "sha256-aTPgnuBvOIqg1+Sa2ZLdMTLujm8dKGK5xpZ3qHpr3f8=";
  cargoBuildFlags = [
    "--package"
    "wasmtime-c-api"
    "--no-default-features"
    "--features cranelift,wasi,pooling-allocator,wat,demangle,gc-null"
  ];

  outputs = [
    "out"
    "lib"
  ];

  nativeBuildInputs = [
    cmake
  ];

  doCheck =
    with stdenv.buildPlatform;
    # SIMD tests are only executed on platforms that support all
    # required processor features (e.g. SSE3, SSSE3 and SSE4.1 on x86_64):
    # https://github.com/bytecodealliance/wasmtime/blob/v9.0.0/cranelift/codegen/src/isa/x64/mod.rs#L220
    (isx86_64 -> sse3Support && ssse3Support && sse4_1Support)
    &&
      # The dependency `wasi-preview1-component-adapter` fails to build because of:
      # error: linker `rust-lld` not found
      !isAarch64;

  postInstall =
    let
      inherit (stdenv.hostPlatform.rust) cargoShortTarget;
    in
    ''
      moveToOutput lib $lib
      ${lib.optionalString (!enableShared) "rm -f $lib/lib/*.so{,.*}"}
      ${lib.optionalString (!enableStatic) "rm -f $lib/lib/*.a"}

      # copy the build.rs generated c-api headers
      # https://github.com/rust-lang/cargo/issues/9661
      mkdir -p $out
      cp -r target/${cargoShortTarget}/release/build/wasmtime-c-api-impl-*/out/include $out/include
    ''
    + lib.optionalString stdenv.hostPlatform.isDarwin ''
      install_name_tool -id \
        $lib/lib/libwasmtime.dylib \
        $lib/lib/libwasmtime.dylib
    '';
})
