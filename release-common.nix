{ pkgs }:

rec {
  sh = pkgs.busybox.override {
    useMusl = true;
    enableStatic = true;
    enableMinimal = true;
    extraConfig = ''
      CONFIG_ASH y
      CONFIG_ASH_ECHO y
      CONFIG_ASH_TEST y
      CONFIG_ASH_OPTIMIZE_FOR_SIZE y
    '';
  };

  configureFlags =
    [ "--disable-init-state"
      "--enable-gc"
    ] ++ pkgs.lib.optionals pkgs.stdenv.isLinux [
      "--with-sandbox-shell=${sh}/bin/busybox"
    ];
}
