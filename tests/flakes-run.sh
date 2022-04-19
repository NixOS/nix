source common.sh

clearStore
rm -rf $TEST_HOME/.cache $TEST_HOME/.config $TEST_HOME/.local
cp ./shell-hello.nix ./config.nix $TEST_HOME
cd $TEST_HOME

cat <<EOF > flake.nix
{
    outputs = {self}: {
      packages.$system.PkgAsPkg = (import ./shell-hello.nix).hello;
      packages.$system.AppAsApp = self.packages.$system.AppAsApp;

      apps.$system.PkgAsApp = self.packages.$system.PkgAsPkg;
      apps.$system.AppAsApp = {
        type = "app";
        program = "\${(import ./shell-hello.nix).hello}/bin/hello";
      };
    };
}
EOF
nix run --no-write-lock-file .#AppAsApp
nix run --no-write-lock-file .#PkgAsPkg

! nix run --no-write-lock-file .#PkgAsApp || fail "'nix run' shouldn’t accept an 'app' defined under 'packages'"
! nix run --no-write-lock-file .#AppAsPkg || fail "elements of 'apps' should be of type 'app'"

clearStore

