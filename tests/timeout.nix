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

}
