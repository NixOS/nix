{
  runCommand,
  system,
  buildPackages,
  cacert,
  nix,
}:

let

  installerClosureInfo = buildPackages.closureInfo {
    rootPaths = [
      nix
      cacert
    ];
  };

  inherit (nix) version;

  env = {
    #nativeBuildInputs = lib.optional (system != "aarch64-linux") shellcheck;
    meta.description = "Distribution-independent Nix bootstrap binaries for ${system}";
  };

in

runCommand "nix-binary-tarball-${version}" env ''
  cp ${installerClosureInfo}/registration $TMPDIR/reginfo
  cp ${../scripts/create-darwin-volume.sh} $TMPDIR/create-darwin-volume.sh
  substitute ${../scripts/install-nix-from-tarball.sh} $TMPDIR/install \
    --subst-var-by nix ${nix} \
    --subst-var-by cacert ${cacert}

  substitute ${../scripts/install-darwin-multi-user.sh} $TMPDIR/install-darwin-multi-user.sh \
    --subst-var-by nix ${nix} \
    --subst-var-by cacert ${cacert}
  substitute ${../scripts/install-systemd-multi-user.sh} $TMPDIR/install-systemd-multi-user.sh \
    --subst-var-by nix ${nix} \
    --subst-var-by cacert ${cacert}
  substitute ${../scripts/install-freebsd-multi-user.sh} $TMPDIR/install-freebsd-multi-user.sh \
    --subst-var-by nix ${nix} \
    --subst-var-by cacert ${cacert}
  substitute ${../scripts/install-multi-user.sh} $TMPDIR/install-multi-user \
    --subst-var-by nix ${nix} \
    --subst-var-by cacert ${cacert}

  if type -p shellcheck; then
    # SC1090: Don't worry about not being able to find
    #         $nix/etc/profile.d/nix.sh
    shellcheck --exclude SC1090 $TMPDIR/install
    shellcheck $TMPDIR/create-darwin-volume.sh
    shellcheck $TMPDIR/install-darwin-multi-user.sh
    shellcheck $TMPDIR/install-systemd-multi-user.sh
    shellcheck $TMPDIR/install-freebsd-multi-user.sh

    # SC1091: Don't panic about not being able to source
    #         /etc/profile
    # SC2002: Ignore "useless cat" "error", when loading
    #         .reginfo, as the cat is a much cleaner
    #         implementation, even though it is "useless"
    # SC2116: Allow ROOT_HOME=$(echo ~root) for resolving
    #         root's home directory
    shellcheck --external-sources \
      --exclude SC1091,SC2002,SC2116 $TMPDIR/install-multi-user
  fi

  chmod +x $TMPDIR/install
  chmod +x $TMPDIR/create-darwin-volume.sh
  chmod +x $TMPDIR/install-darwin-multi-user.sh
  chmod +x $TMPDIR/install-systemd-multi-user.sh
  chmod +x $TMPDIR/install-freebsd-multi-user.sh
  chmod +x $TMPDIR/install-multi-user
  dir=nix-${version}-${system}
  fn=$out/$dir.tar.xz
  mkdir -p $out/nix-support
  echo "file binary-dist $fn" >> $out/nix-support/hydra-build-products
  tar cfJ $fn \
    --owner=0 --group=0 --mode=u+rw,uga+r \
    --mtime='1970-01-01' \
    --absolute-names \
    --hard-dereference \
    --transform "s,$TMPDIR/install,$dir/install," \
    --transform "s,$TMPDIR/create-darwin-volume.sh,$dir/create-darwin-volume.sh," \
    --transform "s,$TMPDIR/reginfo,$dir/.reginfo," \
    --transform "s,$NIX_STORE,$dir/store,S" \
    $TMPDIR/install \
    $TMPDIR/create-darwin-volume.sh \
    $TMPDIR/install-darwin-multi-user.sh \
    $TMPDIR/install-systemd-multi-user.sh \
    $TMPDIR/install-freebsd-multi-user.sh \
    $TMPDIR/install-multi-user \
    $TMPDIR/reginfo \
    $(cat ${installerClosureInfo}/store-paths)
''
