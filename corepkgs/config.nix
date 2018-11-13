let
  fromEnv = var: def:
    let val = builtins.getEnv var; in
    if val != "" then val else def;
in rec {
  shell = "/usr/bin/bash";
  coreutils = "/usr/bin";
  bzip2 = "/mingw64/bin/bzip2";
  gzip = "/usr/bin/gzip";
  xz = "/mingw64/bin/xz";
  tar = "/usr/bin/tar";
  tarFlags = "--warning=no-timestamp";
  tr = "/usr/bin/tr";
  nixBinDir = fromEnv "NIX_BIN_DIR" "/usr/bin";
  nixPrefix = "/usr";
  nixLibexecDir = fromEnv "NIX_LIBEXEC_DIR" "/usr/libexec";
  nixLocalstateDir = "/nix/var";
  nixSysconfDir = "/usr/etc";
  nixStoreDir = fromEnv "NIX_STORE_DIR" "/nix/store";

  # If Nix is installed in the Nix store, then automatically add it as
  # a dependency to the core packages. This ensures that they work
  # properly in a chroot.
  chrootDeps =
    if dirOf nixPrefix == builtins.storeDir then
      [ (builtins.storePath nixPrefix) ]
    else
      [ ];
}
