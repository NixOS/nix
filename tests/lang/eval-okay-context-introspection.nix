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

  combo-path = "${path}${drv.outPath}${drv.foo.outPath}${drv.drvPath}";
  legit-context = builtins.getContext combo-path;

  reconstructed-path = builtins.appendContext
    (builtins.unsafeDiscardStringContext combo-path)
    desired-context;

  # Eta rule for strings with context.
  etaRule = str:
    str == builtins.appendContext
      (builtins.unsafeDiscardStringContext str)
      (builtins.getContext str);

in [
  (legit-context == desired-context)
  (reconstructed-path == combo-path)
  (etaRule "foo")
  (etaRule drv.drvPath)
  (etaRule drv.foo.outPath)
  (etaRule (builtins.unsafeDiscardOutputDependency drv.drvPath))
]
