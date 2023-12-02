{ nix
, doxygen
}:

nix.overrideAttrs (old: {
  pname = "nix-internal-api-docs";

  configureFlags = old.configureFlags ++ [
    "--enable-internal-api-docs"
  ];
  nativeBuildInputs = old.nativeBuildInputs ++ [
    doxygen
  ];

  dontBuild = true;
  doCheck = false;

  installTargets = [ "internal-api-html" ];

  postInstall = ''
    mkdir -p $out/nix-support
    echo "doc internal-api-docs $out/share/doc/nix/internal-api/html" >> $out/nix-support/hydra-build-products
  '';
})
