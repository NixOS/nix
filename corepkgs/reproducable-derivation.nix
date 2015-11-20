result:
with import ./config.nix;
derivation {
  name = "source-closure";
  builder = shell;
  args = ["-e" (__toFile "name" ''
    #!/bin/bash
    ${coreutils}/mkdir -p $out/nix-support
    ${coreutils}/cat << EOF > $out/nix-support/source
    ${result}
    EOF
    '')];
  system = builtins.currentSystem;
}