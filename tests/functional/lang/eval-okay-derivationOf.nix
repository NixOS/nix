let
  drv = derivation {
    name = "derivation-of";
    builder = "/bin/false";
    system = "x86_64-linux";
    outputs = [
      "out"
      "dev"
    ];
  };

  expectedDrv = builtins.unsafeDiscardOutputDependency drv.drvPath;

  checkOutput = output: [
    (builtins.derivationOf output == expectedDrv)
    (builtins.getContext (builtins.derivationOf output) == builtins.getContext expectedDrv)
  ];

  # This derivation deliberately does not exist. derivationOf must obtain it
  # from the string context without querying the store.
  missingDrv = "/nix/store/00000000000000000000000000000000-missing.drv";
  missingOutput = builtins.appendContext "/nix/store/11111111111111111111111111111111-output" {
    ${missingDrv}.outputs = [ "out" ];
  };
  missingResult = builtins.derivationOf missingOutput;
in
assert builtins ? derivationOf;
builtins.concatMap checkOutput [
  drv.outPath
  drv.dev.outPath
  drv
  drv.dev
  { __toString = self: drv.dev.outPath; }
]
++ [
  (builtins.unsafeDiscardStringContext missingResult == missingDrv)
  (
    builtins.getContext missingResult == {
      ${missingDrv}.path = true;
    }
  )
]
