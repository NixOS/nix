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
  name = "advanced-attributes";
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
  allowedReferences = [ foo ];
  allowedRequisites = [ foo ];
  disallowedReferences = [ bar ];
  disallowedRequisites = [ bar ];
  requiredSystemFeatures = [
    "rainbow"
    "uid-range"
  ];
  preferLocalBuild = true;
  allowSubstitutes = false;
}
