# `NixOS/nix-installer` built with *this* Nix closure embedded, so
# Hydra/CI can dogfood the Rust installer without the (removed)
# `--nix-package-url` knob.
{
  lib,
  stdenv,
  buildPackages,
  runCommand,
  rustPlatform,
  fetchFromGitHub,
  tarball,
}:

let
  installerVersion = "2.34.6";
  src = fetchFromGitHub {
    owner = "NixOS";
    repo = "nix-installer";
    tag = installerVersion;
    hash = "sha256-aTaz8EtHexvke7tGr5MfeKy9g7AraIAFN+dPApm+fds=";
  };

  # Bare binary: no Nix closure yet.  Appended below via `pack`, so the
  # (expensive) Rust compile is independent of the embedded Nix and
  # stays cacheable across Nix revisions.
  bare = rustPlatform.buildRustPackage {
    pname = "nix-installer-bare";
    version = installerVersion;

    inherit src;

    cargoHash = "sha256-/mNXkeZVuYsqd0TiUa7bzSP4xpKh0Fqga9EpasPbrzU=";

    doCheck = false;

    env = lib.optionalAttrs stdenv.hostPlatform.isDarwin {
      # Drop the unused libiconv dylib the darwin stdenv injects; the
      # binary must run before `/nix/store` exists.
      NIX_LDFLAGS = "-dead_strip_dylibs";
    };

    postInstall = ''
      install -m755 nix-installer.sh $out/bin/nix-installer.sh
    '';
  };
in

runCommand "nix-installer-${tarball.passthru.nixVersion}"
  {
    nativeBuildInputs = [
      buildPackages.python3
    ]
    ++ lib.optionals stdenv.hostPlatform.isDarwin [
      buildPackages.darwin.sigtool
      buildPackages.darwin.cctools
    ];

    # The appended payload contains store-path strings on purpose; don't
    # let the reference scanner pull the whole Nix closure into this
    # derivation's runtime closure.
    __structuredAttrs = true;
    unsafeDiscardReferences.out = true;

    passthru = { inherit bare; };

    meta = {
      description = "Rust-based Nix installer with an embedded Nix ${tarball.passthru.nixVersion}";
      homepage = "https://github.com/NixOS/nix-installer";
      license = lib.licenses.lgpl21Only;
      mainProgram = "nix-installer";
    };
  }
  ''
    mkdir -p $out/bin $out/nix-support

    python3 ${src}/scripts/pack \
      --input ${bare}/bin/nix-installer \
      --tarball ${tarball}/nix.tar.zst \
      --nix-store-path ${tarball.passthru.nixStorePath} \
      --cacert-store-path ${tarball.passthru.cacertStorePath} \
      --nix-version ${tarball.passthru.nixVersion} \
      --output $out/bin/nix-installer

    install -m755 ${bare}/bin/nix-installer.sh $out/bin/nix-installer.sh

    echo "file binary-dist $out/bin/nix-installer" >> $out/nix-support/hydra-build-products
    echo "file binary-dist $out/bin/nix-installer.sh" >> $out/nix-support/hydra-build-products
  ''
