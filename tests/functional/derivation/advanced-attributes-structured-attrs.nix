{ contentAddress }:

let
  caArgs =
    if contentAddress then
      {
        __contentAddressed = true;
        outputHashMode = "recursive";
        outputHashAlgo = "sha256";
      }
    else
      { };

  derivation' = args: derivation (caArgs // args);

  system = "my-system";

  foo = derivation' {
    inherit system;
    name = "foo";
    builder = "/bin/bash";
    args = [
      "-c"
      "echo foo > $out"
    ];
    outputs = [
      "out"
      "dev"
    ];
  };

  bar = derivation' {
    inherit system;
    name = "bar";
    builder = "/bin/bash";
    args = [
      "-c"
      "echo bar > $out"
    ];
    outputs = [
      "out"
      "dev"
    ];
  };

in
derivation' {
  inherit system;
  name = "advanced-attributes-structured-attrs";
  builder = "/bin/bash";
  args = [
    "-c"
    "echo hello > $out"
  ];
  __sandboxProfile = "sandcastle";
  __noChroot = true;
  __impureHostDeps = [ "/usr/bin/ditto" ];
  impureEnvVars = [ "UNICORN" ];
  __darwinAllowLocalNetworking = true;
  outputs = [
    "out"
    "bin"
    "dev"
  ];
  __structuredAttrs = true;
  outputChecks = {
    out = {
      allowedReferences = [ foo ];
      allowedRequisites = [
        foo.dev
        "bin"
      ];
    };
    bin = {
      disallowedReferences = [
        bar
        "dev"
      ];
      disallowedRequisites = [ bar.dev ];
    };
    dev = {
      maxSize = 789;
      maxClosureSize = 5909;
    };
  };
  requiredSystemFeatures = [
    "rainbow"
    "uid-range"
  ];
  preferLocalBuild = true;
  allowSubstitutes = false;
  exportReferencesGraph.refs1 = [ foo ];
  exportReferencesGraph.refs2 = [ bar.drvPath ];
}
