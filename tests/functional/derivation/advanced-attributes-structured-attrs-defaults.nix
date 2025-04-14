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
  name = "advanced-attributes-structured-attrs-defaults";
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
}
