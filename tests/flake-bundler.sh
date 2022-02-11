source common.sh

clearStore
rm -rf $TEST_HOME/.cache $TEST_HOME/.config $TEST_HOME/.local

cp ./simple.nix ./simple.builder.sh ./config.nix $TEST_HOME

cd $TEST_HOME

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
nix bundle --bundler .#bundlers.$system.default .#packages.$system.default
nix bundle --bundler .#bundlers.$system.simple  .#packages.$system.default

nix bundle --bundler .#bundlers.$system.default .#apps.$system.default
nix bundle --bundler .#bundlers.$system.simple  .#apps.$system.default

clearStore

