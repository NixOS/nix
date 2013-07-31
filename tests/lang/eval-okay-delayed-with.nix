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
      inherit a;
    };

    c = b;
  };

  packageOverrides = pkgs: with pkgs; {
    b = derivation (b.drvAttrs // { name = "${b.name}-overridden"; });
  };

  pkgs = pkgs_ // (packageOverrides pkgs_);

in "${pkgs.a.b.name} ${pkgs.c.name} ${pkgs.b.a.name}"
