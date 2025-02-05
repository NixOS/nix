{
  lib,
  callPackage,
  makeRustPlatform,
  nix,
  rust-bin,
  ...
}:

let
  inherit (lib) fileset;

  toolchain-stable = rust-bin.stable.latest;
  toolchain-nightly = rust-bin.nightly."2025-01-30";

  hooks = callPackage ../../hooks {
    inherit nix-util-rust toolchain-stable toolchain-nightly;
    bridgeDescription = nix-util-rust.meta.description;
    bridgeVersion = nix-util-rust.version;
    crateSubdir = "nix-util-rust";
  };

  rustPlatform = makeRustPlatform {
    cargo = rust-bin.stable.latest.default;
    rustc = rust-bin.stable.latest.default;
  };

  pname = "nix-util-rust";

  nix-util-rust = rustPlatform.buildRustPackage {
    inherit pname;
    version = nix.version;

    outputs = [
      "out"
      "dev"
      "cxxbridge"
    ];

    nativeBuildInputs = [
      hooks.cargoClippyHook
      hooks.cargoDenyHook
      hooks.cargoEditMetadataHook
      hooks.cargoFmtCheckHook
      hooks.cxxCopyTargetBridgesHook
    ];

    cargoLock = {
      lockFile = ../../Cargo.lock;
    };

    src = fileset.toSource {
      root = ../../.;
      fileset = fileset.unions [
        ../../Cargo.lock
        ../../Cargo.toml
        ../../crates
        ../../deny.toml
        ../../rustfmt.toml
      ];
    };
    buildAndTestSubdir = "crates/nix-util-rust";

    preBuild = ''
      cargoEditMetadataHook
      cargoFmtCheckHook
      cargoClippyHook
      cargoDenyHook
    '';

    postInstall = ''
      cxxCopyTargetBridgesHook $dev $cxxbridge
    '';

    meta = {
      inherit (nix.meta) platforms;
      description = "The Nix Rust cxx Bridge";
      maintainers = [
        {
          # TODO: add to nixpkgs
          email = "silvanshade@users.noreply.github.com";
          github = "silvanshade";
          githubId = "11022302";
          name = "silvanshade";
        }
      ];
    };
  };
in
nix-util-rust
