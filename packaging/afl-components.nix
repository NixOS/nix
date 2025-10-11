# AFL++-instrumented component scope for fuzzing
# Similar to ci/gha/tests/default.nix but for AFL++ instead of just sanitizers

{
  lib,
  pkgs,
  nixComponents,
  aflplusplus,
}:

let
  # Layer that adds AFL++ LTO instrumentation and sanitizers to all components
  enableAFLInstrumentationLayer = finalAttrs: prevAttrs: {
    preConfigure = (prevAttrs.preConfigure or "") + ''
      echo "Configuring AFL++ LTO instrumentation with sanitizers for ${
        finalAttrs.pname or "component"
      }..."
      export CC="${aflplusplus}/bin/afl-clang-lto"
      export CXX="${aflplusplus}/bin/afl-clang-lto++"
      export AFL_QUIET=1
      export AFL_USE_ASAN=1
      export AFL_USE_UBSAN=1
    '';

    mesonFlags = (prevAttrs.mesonFlags or [ ]) ++ [
      (lib.mesonOption "b_sanitize" "address,undefined")
      (lib.mesonBool "b_lundef" false)
    ];
  };

in

# Override the component scope to add AFL++ instrumentation to everything
nixComponents.overrideScope (
  final: prev: {
    # Apply AFL++ instrumentation layer to all meson components
    mesonComponentOverrides = lib.composeManyExtensions [
      (prev.mesonComponentOverrides or (_: _: { }))
      enableAFLInstrumentationLayer
    ];

    # Disable GC (Boehm is incompatible with ASAN)
    nix-expr = prev.nix-expr.override { enableGC = false; };

    # Disable Perl bindings (incompatible with dynamically linked ASAN)
    nix-perl-bindings = null;

    # Define nix-expr-fuzz HERE so it picks up the AFL-instrumented dependencies
    nix-expr-fuzz = final.callPackage ../src/libexpr-fuzz/package.nix {
      inherit aflplusplus;
    };
  }
)
