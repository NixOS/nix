let
  pkgs_ = with pkgs; {
    a = derivation {
      name = "a";
      system = builtins.currentSystem;
      builder = "/bin/sh";
      args = [ "-c" "touch $out" ];
      inherit b;
    };

    b = derivation {
      name = "b";
      system = builtins.currentSystem;
      builder = "/bin/sh";
      args = [ "-c" "touch $out" ];
    };

    c = b;
  };

  packageOverrides = p: {
    b = derivation (p.b.drvAttrs // { name = "b-overridden"; });
  };

  pkgs = pkgs_ // (packageOverrides pkgs_);
in pkgs.a.b.name
