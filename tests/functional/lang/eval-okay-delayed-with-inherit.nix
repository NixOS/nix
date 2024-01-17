let
  pkgs_ = with pkgs; {
    a = derivation {
      name = "a";
      system = builtins.currentSystem;
      builder = "/bin/sh";
      args = [ "-c" "touch $out" ];
      inherit b;
    };

    inherit b;
  };

  packageOverrides = p: {
    b = derivation {
      name = "b-overridden";
      system = builtins.currentSystem;
      builder = "/bin/sh";
      args = [ "-c" "touch $out" ];
    };
  };

  pkgs = pkgs_ // (packageOverrides pkgs_);
in pkgs.a.b.name
