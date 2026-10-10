{
  lib,
  nixInstallers,
  runCommand,
  version,
}:

runCommand "nix-installer-script-${version}"
  {
    allowedRequisites = [ ];
  }
  ''
    mkdir -p $out/nix-support

    substitute ${./nix-installer.in} $out/nix-installer \
    ${
      lib.concatMapStrings (
        nixInstaller:
        let
          inherit (nixInstaller.stdenv.hostPlatform) system;
        in
        ''
          --replace-fail '@nixInstallerHash_${system}@' $(sha256sum -b ${nixInstaller}/bin/nix-installer | cut -c1-64) \
        ''
      ) nixInstallers
    } --replace-fail '@nixVersion@' ${version}

    echo "file installer $out/nix-installer" >> $out/nix-support/hydra-build-products
  ''
