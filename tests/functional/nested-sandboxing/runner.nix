{ altitude, storeFun }:

with import ../config.nix;

mkDerivation {
  name = "nested-sandboxing";
  busybox = builtins.getEnv "busybox";
  EXTRA_SANDBOX = builtins.getEnv "EXTRA_SANDBOX";
  buildCommand = if altitude == 0 then ''
    echo Deep enough! > $out
  '' else ''
    cp -r ${../common} ./common
    cp ${../common.sh} ./common.sh
    cp ${../config.nix} ./config.nix
    cp -r ${./.} ./nested-sandboxing

    export PATH=${builtins.getEnv "NIX_BIN_DIR"}:$PATH

    source common.sh
    source ./nested-sandboxing/command.sh

    runNixBuild ${storeFun} ${toString altitude} >> $out
  '';
}
