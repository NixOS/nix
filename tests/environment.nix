{ var, value }:

with import ./config.nix;

mkDerivation {
  name = "test";
  buildCommand = ''
    echo ${var} = "''$${var}"
    echo -n "''$${var}" > "$out"
  '';

  impureEnvVars = [ var ];

  outputHashAlgo = "sha256";
  outputHash = builtins.hashString "sha256" value;
}
