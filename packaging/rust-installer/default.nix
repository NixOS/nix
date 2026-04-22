# `NixOS/nix-installer` built with *this* Nix closure embedded, so
# Hydra/CI can dogfood the Rust installer without the (removed)
# `--nix-package-url` knob.
{
  lib,
  stdenv,
  rustPlatform,
  fetchFromGitHub,
  tarball,
}:

let
  installerVersion = "2.34.5";
  src = fetchFromGitHub {
    owner = "NixOS";
    repo = "nix-installer";
    tag = installerVersion;
    hash = "sha256-+gM241qQOzQlOnP0a7d47z3iRf9+yNjbBJCLIWWNX+c=";
  };
in

rustPlatform.buildRustPackage {
  pname = "nix-installer";
  version = tarball.passthru.nixVersion;

  inherit src;

  cargoHash = "sha256-6pt2f7wznH672L5+SkbA5GA6Sxvk1KamAf3erGqZlLU=";

  doCheck = false;

  env = {
    NIX_TARBALL_PATH = "${tarball}/nix.tar.zst";
    NIX_STORE_PATH = tarball.passthru.nixStorePath;
    NSS_CACERT_STORE_PATH = tarball.passthru.cacertStorePath;
    NIX_VERSION = tarball.passthru.nixVersion;
  }
  // lib.optionalAttrs stdenv.hostPlatform.isDarwin {
    # Drop the unused libiconv dylib the darwin stdenv injects; the
    # binary must run before `/nix/store` exists.
    NIX_LDFLAGS = "-dead_strip_dylibs";
  };

  postInstall = ''
    install -m755 nix-installer.sh $out/bin/nix-installer.sh

    mkdir -p $out/nix-support
    echo "file binary-dist $out/bin/nix-installer" >> $out/nix-support/hydra-build-products
    echo "file binary-dist $out/bin/nix-installer.sh" >> $out/nix-support/hydra-build-products
  '';

  # The binary embeds store-path strings (`NIX_STORE_PATH`, …) on
  # purpose; don't let the reference scanner pull the whole Nix
  # closure into this derivation's runtime closure.
  __structuredAttrs = true;
  unsafeDiscardReferences.out = true;

  meta = {
    description = "Rust-based Nix installer with an embedded Nix ${tarball.passthru.nixVersion}";
    homepage = "https://github.com/NixOS/nix-installer";
    license = lib.licenses.lgpl21Only;
    mainProgram = "nix-installer";
  };
}
