{
  lib,
  nix-cli,
  runCommand,
  stdenv,
}:

# Run this package outside a Nix build: Seatbelt profiles cannot be nested.
runCommand "darwin-network-sandbox-test"
  {
    nativeBuildInputs = [ stdenv.cc ];

    meta.platforms = lib.platforms.darwin;
  }
  ''
    mkdir -p "$out/bin" "$out/libexec"

    $CC -std=c11 -Wall -Wextra -Werror ${./probe.c} -o "$out/libexec/network-sandbox-probe"

    substitute ${./run.sh.in} "$out/bin/darwin-network-sandbox-test" \
      --replace-fail @derivations@ ${./derivations.nix} \
      --replace-fail @nixBin@ ${nix-cli}/bin \
      --replace-fail @probe@ "$out/libexec/network-sandbox-probe"

    chmod +x "$out/bin/darwin-network-sandbox-test"
  ''
