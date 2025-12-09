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

in
derivation' {
  inherit system;
  name = "advanced-attributes-structured-attrs-defaults-null";
  builder = "/bin/bash";
  args = [
    "-c"
    "echo hello > $out"
  ];
  outputs = [
    "out"
    "dev"
  ];
  __structuredAttrs = true;
  outputChecks = {
    out = {
      # Test that null is treated as "not set" (no restriction)
      allowedReferences = null;
      allowedRequisites = null;
    };
  };
}
