# Zstd-compressed Nix closure in the layout expected to be embedded into
# nix-installer binary.
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

    # Store mtime is 1 second into the epoch.
    tar cf - \
      --sort=name \
      --owner=0 --group=0 --mode=u+rw,uga+r \
      --mtime='@1' \
      --absolute-names \
      --hard-dereference \
      --transform "s,$TMPDIR/reginfo,$dir/.reginfo," \
      --transform "s,$NIX_STORE,$dir/store,S" \
      $TMPDIR/reginfo \
      $(cat ${installerClosureInfo}/store-paths) \
      | zstd -19 -T1 -o $out/nix.tar.zst
  ''
