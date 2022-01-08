{ var, value }:

derivation {
  name = "test";
  system = builtins.currentSystem;
  builder = "/bin/sh";
  args = [ "-c" ''
    echo ${var} = "''$${var}"; echo "''$${var}" > "$out"'' ];
  impureEnvVars = [ var ];
  outputHashAlgo = "sha256";
  outputHash = builtins.hashString "sha256" "${value}\n";
}
