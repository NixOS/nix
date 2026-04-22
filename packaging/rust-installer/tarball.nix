# Zstd-compressed Nix closure in the layout expected by
# `NixOS/nix-installer` (`include_bytes!` at build time).
{
  lib,
  stdenv,
  runCommand,
  buildPackages,
  zstd,
  nix,
  cacert,
}:

let
  installerClosureInfo = buildPackages.closureInfo {
    rootPaths = [
      nix
      cacert
    ];
  };
in

runCommand "nix-installer-tarball-${nix.version}"
  {
    nativeBuildInputs = [ zstd ];

    passthru = {
      nixStorePath = nix.outPath;
      cacertStorePath = cacert.outPath;
      nixVersion = nix.version;
    };
  }
  ''
    mkdir -p $out

    dir=nix-${nix.version}-${stdenv.hostPlatform.system}

    cp ${installerClosureInfo}/registration $TMPDIR/reginfo

    tar cf - \
      --sort=name \
      --owner=0 --group=0 --mode=u+rw,uga+r \
      --mtime='1970-01-01' \
      --absolute-names \
      --hard-dereference \
      --transform "s,$TMPDIR/reginfo,$dir/.reginfo," \
      --transform "s,$NIX_STORE,$dir/store,S" \
      $TMPDIR/reginfo \
      $(cat ${installerClosureInfo}/store-paths) \
      | zstd -19 -T1 -o $out/nix.tar.zst
  ''
