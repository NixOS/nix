# `NixOS/nix-installer` built with *this* Nix closure embedded.

{
  lib,
  stdenv,
  buildPackages,
  runCommand,
  rustPlatform,
  pkg-config,
  zstd,
  fetchFromGitHub,
  tarball,
  # Hack around pkgsStatic.buildPackages being prefixed.
  cctools,
  sigtool,
}:

let
  installerVersion = "2.36.0";
  src = fetchFromGitHub {
    owner = "NixOS";
    repo = "nix-installer";
    rev = "71db3b99eb4f0b8c40df2b35c3c6ba37f5a6c317";
    hash = "sha256-/yxaQwcd0MggnuM5iSa136hWwdt8xjqOEk6MHUzDxog=";
  };

  # Bare binary: no Nix closure yet.  Appended below via `pack`, so the
  # (expensive) Rust compile is independent of the embedded Nix and
  # stays cacheable across Nix revisions.
  bare = rustPlatform.buildRustPackage {
    pname = "nix-installer-bare";
    version = installerVersion;

    inherit src;

    cargoHash = "sha256-NZwRUReqDCrYiM7wWxyxOYTklA6fAgID+jO1r6M0lDM=";

    nativeBuildInputs = [
      pkg-config
    ];

    buildInputs = [
      zstd
    ];

    doCheck = false;

    env = lib.optionalAttrs stdenv.hostPlatform.isDarwin {
      # Drop the unused libiconv dylib the darwin stdenv injects; the
      # binary must run before `/nix/store` exists.
      NIX_LDFLAGS = "-dead_strip_dylibs";
    };
  };
in

runCommand "nix-installer-${tarball.passthru.nixVersion}"
  {
    nativeBuildInputs = [
      buildPackages.python3
    ]
    ++ lib.optionals stdenv.hostPlatform.isDarwin [
      sigtool
      cctools
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

    echo "file binary-dist $out/bin/nix-installer" >> $out/nix-support/hydra-build-products
  ''
