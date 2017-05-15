{ pkgs }:

rec {
  sh = pkgs.busybox.override {
    useMusl = true;
    enableStatic = true;
    enableMinimal = true;
    extraConfig = ''
      CONFIG_ASH y
      CONFIG_ASH_BUILTIN_ECHO y
      CONFIG_ASH_BUILTIN_TEST y
      CONFIG_ASH_OPTIMIZE_FOR_SIZE y
    '';
  };

  configureFlags =
    [ "--disable-init-state"
      "--enable-gc"
      "--with-sandbox-shell=${sh}/bin/busybox"
    ];
}
