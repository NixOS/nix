with import ./config.nix;

{

  infiniteLoop = mkDerivation {
    name = "timeout";
    buildCommand = ''
      echo "‘timeout’ builder entering an infinite loop"
      while true ; do echo -n .; done
    '';
  };

  silent = mkDerivation {
    name = "silent";
    buildCommand = ''
      sleep 60
    '';
  };

  closeLog = mkDerivation {
    name = "silent";
    buildCommand = ''
      exec > /dev/null 2>&1
      sleep 1000000000
    '';
  };

}
