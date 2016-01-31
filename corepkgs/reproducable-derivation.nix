{ recording, expressions}:
with import ./config.nix;
derivation {
  name = "source-closure";
  builder = shell;
  args = ["-e" (__toFile "name" ''
    #!/bin/bash
    ${coreutils}/mkdir -p $out/nix-support
    ${coreutils}/cp ${__toFile "deterministic-recording" recording} $out/nix-support/recording.nix
    ${coreutils}/cp ${__toFile "expressions" expressions} $out/nix-support/expressions.nix
    echo "__playback { sources = { \"$out\" = ./.; }; functions = []; }" > $out/default.nix
    echo "(__playback (import ./nix-support/recording.nix) (import ./nix-support/expressions.nix))" >> $out/default.nix
    '')];
  system = builtins.currentSystem;
}