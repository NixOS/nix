{
  self,
  system,
  lib,
  python,
  boost,
  clang-tools,
  pkg-config,
  ninja,
  meson,
  nix,
  mkShell,
  recurseIntoAttrs,
  isShell ? false,
}:
let
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
python.pkgs.buildPythonPackage {
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
    (meson.override { python3 = python; })
  ] ++ lib.optional (!isShell) nix;

  buildInputs = nix.propagatedBuildInputs ++ [
    boost
  ] ++ lib.optional (!isShell) nix;

  mesonBuildType = "release";

  doInstallCheck = true;
  TEST_SCRIPTS = testScripts;
  installCheckPhase = "meson test -v";

  passthru = {
    exampleEnv = python.withPackages (p: [ nix.python-bindings ]);
    tests = {
      example-buildPythonApplication = import ./examples/buildPythonApplication {
        inherit nix system testScripts;
      };
    };
    shell = mkShell {
      packages = [
        clang-tools
      ];
      TEST_SCRIPTS = testScripts;
      inputsFrom = [
        (nix.python-bindings.override { isShell = true; })
      ];
    };
  };
}
