source common.sh

clearStore
rm -rf $TEST_HOME/.cache $TEST_HOME/.config $TEST_HOME/.local
cp ./shell-hello.nix ./config.nix $TEST_HOME
cd $TEST_HOME

cat <<EOF > flake.nix
{
    outputs = {self}: {
      packages.$system.pkgAsPkg = (import ./shell-hello.nix).hello;
      packages.$system.appAsApp = self.packages.$system.appAsApp;

      apps.$system.pkgAsApp = self.packages.$system.pkgAsPkg;
      apps.$system.appAsApp = {
        type = "app";
        program = "\${(import ./shell-hello.nix).hello}/bin/hello";
      };
    };
}
EOF
nix run --no-write-lock-file .#appAsApp
nix run --no-write-lock-file .#pkgAsPkg

! nix run --no-write-lock-file .#pkgAsApp || fail "'nix run' shouldn’t accept an 'app' defined under 'packages'"
! nix run --no-write-lock-file .#appAsPkg || fail "elements of 'apps' should be of type 'app'"

clearStore

