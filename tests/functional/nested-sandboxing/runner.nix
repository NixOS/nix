{ altitude, storeFun }:

with import ../config.nix;

mkDerivation {
  name = "nested-sandboxing";
  busybox = builtins.getEnv "busybox";
  EXTRA_SANDBOX = builtins.getEnv "EXTRA_SANDBOX";
  buildCommand = ''
    set -x
    set -eu -o pipefail
  ''
  + (
    if altitude == 0 then
      ''
        echo Deep enough! > $out
      ''
    else
      ''
        cp -r ${../common} ./common
        cp ${../common.sh} ./common.sh
        cp ${../config.nix} ./config.nix
        cp -r ${./.} ./nested-sandboxing

        export PATH=${builtins.getEnv "NIX_BIN_DIR"}:$PATH

        export _NIX_TEST_SOURCE_DIR=$PWD
        export _NIX_TEST_BUILD_DIR=$PWD

        source common.sh
        source ./nested-sandboxing/command.sh

        runNixBuild ${storeFun} ${toString altitude} >> $out
      ''
  );
}
