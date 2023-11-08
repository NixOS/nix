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

  # Only holds true if string context contains both a `DrvDeep` and
  # `Opaque` element.
  almostEtaRule = str:
    str == builtins.addDrvOutputDependencies
      (builtins.unsafeDiscardOutputDependency str);

  addDrvOutputDependencies_idempotent = str:
    builtins.addDrvOutputDependencies str ==
    builtins.addDrvOutputDependencies (builtins.addDrvOutputDependencies str);

  rules = str: [
    (etaRule str)
    (almostEtaRule str)
    (addDrvOutputDependencies_idempotent str)
  ];

in [
  (legit-context == desired-context)
  (reconstructed-path == combo-path)
  (etaRule "foo")
  (etaRule drv.foo.outPath)
] ++ builtins.concatMap rules [
  drv.drvPath
  (builtins.addDrvOutputDependencies drv.drvPath)
  (builtins.unsafeDiscardOutputDependency drv.drvPath)
]
