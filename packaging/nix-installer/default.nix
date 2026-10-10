# `NixOS/nix-installer` built with *this* Nix closure embedded.

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
  installerVersion = "2.36.0pre";
  src = fetchFromGitHub {
    owner = "NixOS";
    repo = "nix-installer";
    # Don't forget to bump this after release branch-off.
    # TODO: Document this stuff in release-process.md.
    rev = "fb0541d1785dcd5e252cbe05d08c6cba64da751f";
    hash = "sha256-ZOZwGzo6P70ymfzwYwe+36KLLWwevBNEEUf1CW1nCqw=";
  };

  # Bare binary: no Nix closure yet.  Appended below via `pack`, so the
  # (expensive) Rust compile is independent of the embedded Nix and
  # stays cacheable across Nix revisions.
  bare = rustPlatform.buildRustPackage {
    pname = "nix-installer-bare";
    version = installerVersion;

    inherit src;

    cargoHash = "sha256-oNDsyjFCC7B9TovJqdZoXQwr7dtdFdUNltXOgv0aPLk=";

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

    echo "file binary-dist $out/bin/nix-installer" >> $out/nix-support/hydra-build-products
  ''
