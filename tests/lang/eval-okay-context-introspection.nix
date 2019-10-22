let
  drv = derivation {
    name = "fail";
    builder = "/bin/false";
    system = "x86_64-linux";
    outputs = [ "out" "foo" ];
  };

  path = "${./eval-okay-context-introspection.nix}";

  desired-context = {
    "${builtins.unsafeDiscardStringContext path}" = {
      path = true;
    };
    "${builtins.unsafeDiscardStringContext drv.drvPath}" = {
      outputs = [ "foo" "out" ];
      allOutputs = true;
    };
  };

  legit-context = builtins.getContext "${path}${drv.outPath}${drv.foo.outPath}${drv.drvPath}";

  constructed-context = builtins.getContext (builtins.appendContext "" desired-context);
in legit-context == constructed-context
