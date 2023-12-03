{ lib
, runCommand
, nix
, systemTarballPairs
}:

runCommand "installer-script" {
  buildInputs = [ nix ];
} ''
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
    ${lib.concatMapStrings
      ({ system, tarball }:
        '' \
        --replace '@tarballHash_${system}@' $(nix --experimental-features nix-command hash-file --base16 --type sha256 ${tarball}/*.tar.xz) \
        --replace '@tarballPath_${system}@' $(tarballPath ${tarball}/*.tar.xz) \
        ''
      )
      systemTarballPairs
    } --replace '@nixVersion@' ${nix.version}

  echo "file installer $out/install" >> $out/nix-support/hydra-build-products
''
