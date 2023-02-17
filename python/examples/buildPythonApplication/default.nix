{
  nixpkgsSrc ? fetchTarball "https://github.com/NixOS/nixpkgs/archive/545c7a31e5dedea4a6d372712a18e00ce097d462.tar.gz",
  nixSrc ? fetchTarball "https://github.com/tweag/nix/archive/9b49f6902c5511e96a2a822bc36f245caf78de3f.tar.gz",
}: let
  pkgs = import nixpkgsSrc { overlays = []; config = {}; };
  nix = (import nixSrc).default;
  python = pkgs.python3;
  nixBindings = nix.python-bindings.override { inherit python; };
in python.pkgs.buildPythonApplication {
  pname = "hello-nix";
  version = "0.1";
  src = ./.;
  propagatedBuildInputs = [
    nixBindings
    #python.pkgs.requests
  ];
  #preBuild = ''
  #  echo $PYTHONPATH
  #  exit 1
  #'';
  dontUseSetuptoolsShellHook = true;
  passthru.nixBindings = nixBindings;
  #checkPhase = ''
  #  ls -laa $out/hello-nix
  #'';
}
