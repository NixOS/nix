result:
with import ./config.nix;
derivation {
  name = "source-closure";
  builder = shell;
  args = ["-e" (__toFile "name" ''
    #!/bin/bash
    ${coreutils}/mkdir -p $out/nix-support
    ${coreutils}/cp ${__toFile "result" result} $out/nix-support/source
    '')];
  system = builtins.currentSystem;
}