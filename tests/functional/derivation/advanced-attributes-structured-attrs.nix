let
  system = "my-system";
  foo = derivation {
    inherit system;
    name = "foo";
    builder = "/bin/bash";
    args = [
      "-c"
      "echo foo > $out"
    ];
  };
  bar = derivation {
    inherit system;
    name = "bar";
    builder = "/bin/bash";
    args = [
      "-c"
      "echo bar > $out"
    ];
  };
in
derivation {
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
      allowedRequisites = [ foo ];
    };
    bin = {
      disallowedReferences = [ bar ];
      disallowedRequisites = [ bar ];
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
}
