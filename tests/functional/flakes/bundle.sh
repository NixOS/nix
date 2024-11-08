#!/usr/bin/env bash

source common.sh

cp ../simple.nix ../simple.builder.sh "${config_nix}" "$TEST_HOME"

# `config.nix` cannot be gotten via build dir / env var (runs afoul pure eval). Instead get from flake.
removeBuildDirRef "$TEST_HOME"/*.nix

cd "$TEST_HOME"

cat <<EOF > flake.nix
{
    outputs = {self}: {
      bundlers.$system = rec {
        simple = drv:
          if drv?type && drv.type == "derivation"
          then drv
          else self.packages.$system.default;
        default = simple;
      };
      packages.$system.default = import ./simple.nix;
      apps.$system.default = {
        type = "app";
        program = "\${import ./simple.nix}/hello";
      };
    };
}
EOF

nix build .#
nix bundle --bundler .# .#
nix bundle --bundler .#bundlers."$system".default .#packages."$system".default
nix bundle --bundler .#bundlers."$system".simple  .#packages."$system".default

nix bundle --bundler .#bundlers."$system".default .#apps."$system".default
nix bundle --bundler .#bundlers."$system".simple  .#apps."$system".default
