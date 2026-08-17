{
  clangStdenv,
  gccStdenv,
  lib,
  linkFarm,
  meson,
  ninja,
  writeText,
}:

let
  parentMeson = writeText "meson.build" ''
    project('fuzzing-engine-parent', 'cpp', meson_version : '>= 1.8')

    subproject('util')
    subproject('store')
  '';

  mkChildMeson =
    name:
    writeText "meson.build" ''
      project('fuzzing-engine-${name}', 'cpp', meson_version : '>= 1.8')

      cxx = meson.get_compiler('cpp')
      fuzz_dependencies = []

      assert(not get_option('unit-tests'))

      if get_option('fuzzers')
        fuzz_targets = [
          [ '${name}', files('fuzz.cc') ],
        ]
        subdir('fuzz/harnesses')
      endif
    '';

  harness = writeText "fuzz.cc" ''
    #include <cstddef>
    #include <cstdint>

    extern "C" int LLVMFuzzerTestOneInput(
        const std::uint8_t *,
        std::size_t)
    {
        return 0;
    }
  '';

  engineMain = writeText "engine.c" ''
    #include <stddef.h>

    extern int LLVMFuzzerTestOneInput(const unsigned char *, size_t);

    int main(void)
    {
        static const unsigned char input = 0;
        return LLVMFuzzerTestOneInput(&input, 1);
    }
  '';

  gccEngine = gccStdenv.mkDerivation {
    name = "gcc-fuzzing-engine";
    dontUnpack = true;

    buildPhase = ''
      runHook preBuild
      $CC -c ${engineMain} -o engine.o
      runHook postBuild
    '';

    installPhase = ''
      runHook preInstall
      mkdir -p "$out/lib"
      ar rcs "$out/lib/GCC engine with spaces.a" engine.o
      runHook postInstall
    '';
  };

  fixture = linkFarm "fuzzing-engine-fixture" [
    {
      name = "meson.build";
      path = parentMeson;
    }
    {
      name = "meson.options";
      path = ../../meson.options;
    }
    {
      name = "subprojects/util/meson.build";
      path = mkChildMeson "util";
    }
    {
      name = "subprojects/util/meson.options";
      path = ../../src/libutil-tests/meson.options;
    }
    {
      name = "subprojects/util/fuzz.cc";
      path = harness;
    }
    {
      name = "subprojects/util/fuzz/harnesses/meson.build";
      path = ../../nix-meson-build-support/fuzz/meson.build;
    }
    {
      name = "subprojects/store/meson.build";
      path = mkChildMeson "store";
    }
    {
      name = "subprojects/store/meson.options";
      path = ../../src/libstore-tests/meson.options;
    }
    {
      name = "subprojects/store/fuzz.cc";
      path = harness;
    }
    {
      name = "subprojects/store/fuzz/harnesses/meson.build";
      path = ../../nix-meson-build-support/fuzz/meson.build;
    }
  ];

  mkFixture =
    {
      name,
      stdenv,
      src,
      executables,
      fuzzingEngine ? null,
      localFallback ? false,
      runArgs ? "",
    }:
    stdenv.mkDerivation {
      inherit name src;
      strictDeps = true;
      dontUnpack = true;
      dontConfigure = true;

      nativeBuildInputs = [
        meson
        ninja
      ];

      buildPhase =
        let
          mesonFlags = [
            (lib.mesonBool "unit-tests" false)
            (lib.mesonBool "fuzzers" true)
          ]
          ++ lib.optional (fuzzingEngine != null) (lib.mesonOption "fuzzing-engine" fuzzingEngine)
          ++ lib.optionals localFallback [
            (lib.mesonOption "b_sanitize" "fuzzer-no-link")
            (lib.mesonBool "b_lundef" false)
          ];
        in
        ''
          runHook preBuild
          meson setup build "$src" \
            --prefix="$out" \
            ${lib.escapeShellArgs mesonFlags}
          meson compile -C build
          runHook postBuild
        '';

      installPhase = ''
        runHook preInstall
        meson install -C build
        ${lib.concatMapStringsSep "\n" (executable: ''
          test -x "$out/bin/fuzz-${executable}"
          "$out/bin/fuzz-${executable}" ${runArgs}
        '') executables}
        runHook postInstall
      '';
    };

  externalSanitizer = mkFixture {
    name = "fuzzing-engine-external-sanitizer";
    stdenv = clangStdenv;
    src = "${fixture}/subprojects/util";
    executables = [ "util" ];
    fuzzingEngine = "-fsanitize=fuzzer";
    runArgs = "-runs=1";
  };

  parentYield = mkFixture {
    name = "fuzzing-engine-parent-yield";
    stdenv = gccStdenv;
    src = fixture;
    executables = [
      "util"
      "store"
    ];
    fuzzingEngine = "${gccEngine}/lib/GCC engine with spaces.a";
  };

  localFallback = mkFixture {
    name = "fuzzing-engine-local-fallback";
    stdenv = clangStdenv;
    src = "${fixture}/subprojects/util";
    executables = [ "util" ];
    localFallback = true;
    runArgs = "-runs=1";
  };
in

linkFarm "fuzzing-engine" [
  {
    name = "external-sanitize-fuzzer";
    path = externalSanitizer;
  }
  {
    name = "gcc-archive-parent-yield";
    path = parentYield;
  }
  {
    name = "local-fallback";
    path = localFallback;
  }
]
