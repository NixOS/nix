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

    substitute ${./nix-installer.in} $out/nix-installer --replace-fail '@nixVersion@' ${version}

    echo "file installer $out/nix-installer" >> $out/nix-support/hydra-build-products
  ''
