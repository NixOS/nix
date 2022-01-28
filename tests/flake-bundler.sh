source common.sh

clearStore
rm -rf $TEST_HOME/.cache $TEST_HOME/.config $TEST_HOME/.local

cp ./simple.nix ./simple.builder.sh ./config.nix $TEST_HOME

cd $TEST_HOME

cat <<EOF > flake.nix
{
    outputs = {self}: {
       defaultBundler.$system = drv:
          if drv?type && drv.type == "derivation"
          then drv
          else self.defaultPackage.$system;
       defaultPackage.$system = import ./simple.nix;
       defaultApp.$system = {
         type = "app";
         program = "\${import ./simple.nix}/hello";
       };
    };
}
EOF
nix build .#
nix bundle --bundler .# .#
nix bundle --bundler .#defaultBundler.$system .#defaultPackage.$system

clearStore

