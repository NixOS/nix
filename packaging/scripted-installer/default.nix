{
  lib,
  runCommand,
  version,
  tarballs,
}:

runCommand "installer-script" { } ''
  mkdir -p $out/nix-support

  # Converts /nix/store/50p3qk8k...-nix-2.4pre20201102_550e11f/bin/nix to 50p3qk8k.../bin/nix.
  tarballPath() {
    # Remove the store prefix
    local path=''${1#${builtins.storeDir}/}
    # Get the path relative to the derivation root
    local rest=''${path#*/}
    # Get the derivation hash
    local drvHash=''${path%%-*}
    echo "$drvHash/$rest"
  }

  substitute ${./install.in} $out/install \
    ${
      lib.concatMapStrings (
        tarball:
        let
          inherit (tarball.stdenv.hostPlatform) system;
        in
        ''
          --replace-fail '@tarballHash_${system}@' $(sha256sum -b ${tarball}/*.tar.xz | cut -c1-64) \
          --replace-fail '@tarballPath_${system}@' $(tarballPath ${tarball}/*.tar.xz) \
        ''
      ) tarballs
    } --replace-fail '@nixVersion@' ${version}

  echo "file installer $out/install" >> $out/nix-support/hydra-build-products
''
