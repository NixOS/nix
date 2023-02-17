{ self, system, lib, python, ninja, meson, nix, mkShell }:
python.pkgs.buildPythonPackage {
  name = "nix";
  format = "other";

  src = self;

  strictDeps = true;

  nativeBuildInputs = lib.optionals (nix != null) nix.nativeBuildInputs ++ [
    ninja
    (meson.override { python3 = python; })
    nix
  ];

  buildInputs = lib.optionals (nix != null) nix.buildInputs ++ [
    nix
  ];

  # We need to be able to generate tests/common.sh, which requires running
  # `make`, which requires having run `autoreconf` and `./configure`.
  # So we propagate `autoreconfHook` from nix.nativeBuildInputs for that to
  # work, but after that we also need to cd into the python directory and run the
  # meson configure phase for the python bindings.
  # Can't use `postConfigure` for this because that would create a loop since
  # `mesonConfigurePhase` calls `postConfigure` itself.
  # A small problem with this is that `{pre,post}Configure` get run twice
  dontUseMesonConfigure = true;
  preBuild = ''
    cd python
    mesonConfigurePhase
  '';

  mesonBuildType = "release";

  doInstallCheck = true;
  installCheckPhase = "meson test -v";

  passthru.shell = mkShell {
    inputsFrom = [
      self.devShells.${system}.default
      (nix.python-bindings.override { nix = null; })
    ];
  };
}
