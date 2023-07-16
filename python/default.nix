{
  system,
  lib,
  python3,
  boost,
  gdb,
  clang-tools,
  pkg-config,
  ninja,
  meson,
  nix,
  mkShell,
  enableDebugging,
  recurseIntoAttrs,
  isShell ? false,
}:
let
  _python = python3;
in
let
  python3 = _python.override { self = enableDebugging _python; };
  # Extracts tests/init.sh
  testScripts = nix.overrideAttrs (old: {
    name = "nix-test-scripts-${old.version}";
    outputs = [ "out" ];
    separateDebugInfo = false;
    buildPhase = ''
      make tests/{init.sh,common/vars-and-functions.sh}
    '';
    script = ''
      pushd ${placeholder "out"}/libexec >/dev/null
      source init.sh
      popd >/dev/null
    '';
    passAsFile = [ "script" ];
    installPhase = ''
      rm -rf "$out"
      mkdir -p "$out"/{libexec/common,share/bash}
      cp tests/init.sh "$out"/libexec
      cp tests/common/vars-and-functions.sh "$out"/libexec/common

      cp "$scriptPath" "$out"/share/bash/nix-test.sh
    '';
    dontFixup = true;
  });
in
python3.pkgs.buildPythonPackage {
  name = "nix";
  format = "other";

  src = builtins.path {
    path = ./.;
    filter = path: type:
      path == toString ./meson.build
      || path == toString ./tests.py
      || path == toString ./test.sh
      || lib.hasPrefix (toString ./src) path;
  };


  strictDeps = true;

  nativeBuildInputs = [
    ninja
    pkg-config
    (meson.override { inherit python3; })
  ] ++ lib.optional (!isShell) nix;

  buildInputs = nix.propagatedBuildInputs ++ [
    boost
  ] ++ lib.optional (!isShell) nix;

  mesonBuildType = "release";

  doInstallCheck = true;
  TEST_SCRIPTS = testScripts;
  installCheckPhase = "meson test -v";

  passthru = {
    exampleEnv = python3.withPackages (p: [ nix.python-bindings ]);
    tests = {
      example-buildPythonApplication = import ./examples/buildPythonApplication {
        inherit nix system testScripts python3;
      };
    };
    shell = mkShell {
      packages = [
        clang-tools
        gdb
      ];
      TEST_SCRIPTS = testScripts;
      inputsFrom = [
        (nix.python-bindings.override { isShell = true; })
      ];
    };
  };
}
