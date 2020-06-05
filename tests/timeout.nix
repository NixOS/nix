with import ./config.nix;

{

  infiniteLoop = mkDerivation {
    name = "timeout";
    buildCommand = ''
      touch $out
      echo "'timeout' builder entering an infinite loop"
      while true ; do echo -n .; done
    '';
  };

  silent = mkDerivation {
    name = "silent";
    buildCommand = ''
      touch $out
      sleep 60
    '';
  };

  closeLog = mkDerivation {
    name = "silent";
    buildCommand = ''
      touch $out
      exec > /dev/null 2>&1
      sleep 1000000000
    '';
  };

}
