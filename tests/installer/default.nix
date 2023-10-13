{ binaryTarballs
, nixpkgsFor
}:

let

  installScripts = {
    install-default = {
      script = ''
        tar -xf ./nix.tar.xz
        mv ./nix-* nix
        ./nix/install --no-channel-add
      '';
    };

    install-force-no-daemon = {
      script = ''
        tar -xf ./nix.tar.xz
        mv ./nix-* nix
        ./nix/install --no-daemon --no-channel-add
      '';
    };

    install-force-daemon = {
      script = ''
        tar -xf ./nix.tar.xz
        mv ./nix-* nix
        ./nix/install --daemon --no-channel-add
      '';
    };
  };

  mockChannel = pkgs:
    pkgs.runCommandNoCC "mock-channel" {} ''
      mkdir nixexprs
      mkdir -p $out/channel
      echo -n 'someContent' > nixexprs/someFile
      tar cvf - nixexprs | bzip2 > $out/channel/nixexprs.tar.bz2
    '';

  disableSELinux = "sudo setenforce 0";

  images = {

    /*
    "ubuntu-14-04" = {
      image = import <nix/fetchurl.nix> {
        url = "https://app.vagrantup.com/ubuntu/boxes/trusty64/versions/20190514.0.0/providers/virtualbox.box";
        hash = "sha256-iUUXyRY8iW7DGirb0zwGgf1fRbLA7wimTJKgP7l/OQ8=";
      };
      rootDisk = "box-disk1.vmdk";
      system = "x86_64-linux";
    };
    */

    "ubuntu-16-04" = {
      image = import <nix/fetchurl.nix> {
        url = "https://app.vagrantup.com/generic/boxes/ubuntu1604/versions/4.1.12/providers/libvirt.box";
        hash = "sha256-lO4oYQR2tCh5auxAYe6bPOgEqOgv3Y3GC1QM1tEEEU8=";
      };
      rootDisk = "box.img";
      system = "x86_64-linux";
    };

    "ubuntu-22-04" = {
      image = import <nix/fetchurl.nix> {
        url = "https://app.vagrantup.com/generic/boxes/ubuntu2204/versions/4.1.12/providers/libvirt.box";
        hash = "sha256-HNll0Qikw/xGIcogni5lz01vUv+R3o8xowP2EtqjuUQ=";
      };
      rootDisk = "box.img";
      system = "x86_64-linux";
    };

    "fedora-36" = {
      image = import <nix/fetchurl.nix> {
        url = "https://app.vagrantup.com/generic/boxes/fedora36/versions/4.1.12/providers/libvirt.box";
        hash = "sha256-rxPgnDnFkTDwvdqn2CV3ZUo3re9AdPtSZ9SvOHNvaks=";
      };
      rootDisk = "box.img";
      system = "x86_64-linux";
      postBoot = disableSELinux;
    };

    # Currently fails with 'error while loading shared libraries:
    # libsodium.so.23: cannot stat shared object: Invalid argument'.
    /*
    "rhel-6" = {
      image = import <nix/fetchurl.nix> {
        url = "https://app.vagrantup.com/generic/boxes/rhel6/versions/4.1.12/providers/libvirt.box";
        hash = "sha256-QwzbvRoRRGqUCQptM7X/InRWFSP2sqwRt2HaaO6zBGM=";
      };
      rootDisk = "box.img";
      system = "x86_64-linux";
    };
    */

    "rhel-7" = {
      image = import <nix/fetchurl.nix> {
        url = "https://app.vagrantup.com/generic/boxes/rhel7/versions/4.1.12/providers/libvirt.box";
        hash = "sha256-b4afnqKCO9oWXgYHb9DeQ2berSwOjS27rSd9TxXDc/U=";
      };
      rootDisk = "box.img";
      system = "x86_64-linux";
    };

    "rhel-8" = {
      image = import <nix/fetchurl.nix> {
        url = "https://app.vagrantup.com/generic/boxes/rhel8/versions/4.1.12/providers/libvirt.box";
        hash = "sha256-zFOPjSputy1dPgrQRixBXmlyN88cAKjJ21VvjSWUCUY=";
      };
      rootDisk = "box.img";
      system = "x86_64-linux";
      postBoot = disableSELinux;
    };

    "rhel-9" = {
      image = import <nix/fetchurl.nix> {
        url = "https://app.vagrantup.com/generic/boxes/rhel9/versions/4.1.12/providers/libvirt.box";
        hash = "sha256-vL/FbB3kK1rcSaR627nWmScYGKGk4seSmAdq6N5diMg=";
      };
      rootDisk = "box.img";
      system = "x86_64-linux";
      postBoot = disableSELinux;
      extraQemuOpts = "-cpu Westmere-v2";
    };

  };

  testHelperFunctions = builtins.toFile "test-helper-functions" ''
    export TEST_ROOT="$(mktemp -d)"
    export TEST_HOME="/tmp/test-home"
    mkdir -p "$TEST_HOME"
    export NIX_REMOTE=/
    export NIX_LOG_DIR=/nix/var/log/nix
    export NIX_STORE_DIR=/nix/store
    export cacheDir="/tmp/nix-cache-dir"
  
    readLink() {
        ls -l "$1" | sed 's/.*->\ //'
    }

    clearProfiles() {
        echo "Not clearing profiles, because this is a real install"
    }

    clearStore() {
        nix-collect-garbage
    }

    clearCache() {
        rm -rf "$cacheDir"
    }

    clearCacheCache() {
        rm -f $HOME/.cache/nix/binary-cache*
    }

    if [[ $(uname) == Linux ]] && [[ -L /proc/self/ns/user ]] && unshare --user true; then
        _canUseSandbox=1
    fi

    skipTest () {
        echo "$1, skipping this test..." >&2
        exit 99
    }

    requireDaemonNewerThan () {
        isDaemonNewer "$1" || skipTest "Daemon is too old"
    }

    canUseSandbox() {
        [[ ''${_canUseSandbox-} ]]
    }

    requireSandboxSupport () {
        canUseSandbox || skipTest "Sandboxing not supported"
    }

    requireGit() {
        [[ $(type -p git) ]] || skipTest "Git not installed"
    }

    fail() {
        echo "$1" >&2
        exit 1
    }

    # Run a command failing if it didn't exit with the expected exit code.
    #
    # Has two advantages over the built-in `!`:
    #
    # 1. `!` conflates all non-0 codes. `expect` allows testing for an exact
    # code.
    #
    # 2. `!` unexpectedly negates `set -e`, and cannot be used on individual
    # pipeline stages with `set -o pipefail`. It only works on the entire
    # pipeline, which is useless if we want, say, `nix ...` invocation to
    # *fail*, but a grep on the error message it outputs to *succeed*.
    expect() {
        local expected res
        expected="$1"
        shift
        "$@" && res=0 || res="$?"
        if [[ $res -ne $expected ]]; then
            echo "Expected '$expected' but got '$res' while running '\''${*@Q}'" >&2
            return 1
        fi
        return 0
    }

    # Better than just doing `expect ... >&2` because the "Expected..."
    # message below will *not* be redirected.
    expectStderr() {
        local expected res
        expected="$1"
        shift
        "$@" 2>&1 && res=0 || res="$?"
        if [[ $res -ne $expected ]]; then
            echo "Expected '$expected' but got '$res' while running '\''${*@Q}'" >&2
            return 1
        fi
        return 0
    }

    needLocalStore() {
      if [[ "$NIX_REMOTE" == "daemon" ]]; then
        skipTest "Can’t run through the daemon ($1)"
      fi
    }

    # Just to make it easy to find which tests should be fixed
    buggyNeedLocalStore() {
      needLocalStore "$1"
    }

    enableFeatures() {
        local features="$1"
        sed -i 's/experimental-features .*/& '"$features"'/' "$HOME/.config/nix/nix.conf"
    }

    set -x

    onError() {
        set +x
        echo "$0: test failed at:" >&2
        for ((i = 1; i < ''${#BASH_SOURCE[@]}; i++)); do
            if [[ -z ''${BASH_SOURCE[i]} ]]; then break; fi
            echo "  ''${FUNCNAME[i]} in ''${BASH_SOURCE[i]}:''${BASH_LINENO[i-1]}" >&2
        done
    }

    # `grep -v` doesn't work well for exit codes. We want `!(exist line l. l
    # matches)`. It gives us `exist line l. !(l matches)`.
    #
    # `!` normally doesn't work well with `set -e`, but when we wrap in a
    # function it *does*.
    grepInverse() {
        ! grep "$@"
    }

    # A shorthand, `> /dev/null` is a bit noisy.
    #
    # `grep -q` would seem to do this, no function necessary, but it is a
    # bad fit with pipes and `set -o pipefail`: `-q` will exit after the
    # first match, and then subsequent writes will result in broken pipes.
    #
    # Note that reproducing the above is a bit tricky as it depends on
    # non-deterministic properties such as the timing between the match and
    # the closing of the pipe, the buffering of the pipe, and the speed of
    # the producer into the pipe. But rest assured we've seen it happen in
    # CI reliably.
    grepQuiet() {
        grep "$@" > /dev/null
    }

    # The previous two, combined
    grepQuietInverse() {
        ! grep "$@" > /dev/null
    }

    trap onError ERR
  '';
 
  makeTest = imageName: testName:
    let image = images.${imageName}; in
    with nixpkgsFor.${image.system}.native;
    runCommand
      "installer-test-${imageName}-${testName}"
      { buildInputs = [ qemu_kvm openssh rsync ];
        image = image.image;
        postBoot = image.postBoot or "";
        installScript = installScripts.${testName}.script;
        binaryTarball = binaryTarballs.${system};
        nixTests = runCommand "nix-tests" { } ''
          cp -r ${../.} $out
          chmod -R +w $out
          cp ${testHelperFunctions} $out/common/vars-and-functions.sh
        '';
      }
      ''
        shopt -s nullglob

        echo "Unpacking Vagrant box $image..."
        tar xvf $image

        image_type=$(qemu-img info ${image.rootDisk} | sed 's/file format: \(.*\)/\1/; t; d')

        qemu-img create -b ./${image.rootDisk} -F "$image_type" -f qcow2 ./disk.qcow2

        extra_qemu_opts="${image.extraQemuOpts or ""}"

        # Add the config disk, required by the Ubuntu images.
        config_drive=$(echo *configdrive.vmdk || true)
        if [[ -n $config_drive ]]; then
          extra_qemu_opts+=" -drive id=disk2,file=$config_drive,if=virtio"
        fi

        echo "Starting qemu..."
        qemu-kvm -m 4096 -nographic \
          -drive id=disk1,file=./disk.qcow2,if=virtio \
          -netdev user,id=net0,restrict=yes,hostfwd=tcp::20022-:22 -device virtio-net-pci,netdev=net0 \
          $extra_qemu_opts &
        qemu_pid=$!
        trap "kill $qemu_pid" EXIT

        if ! [ -e ./vagrant_insecure_key ]; then
          cp ${./vagrant_insecure_key} vagrant_insecure_key
        fi

        chmod 0400 ./vagrant_insecure_key

        ssh_opts="-o StrictHostKeyChecking=no -o HostKeyAlgorithms=+ssh-rsa -o PubkeyAcceptedKeyTypes=+ssh-rsa -i ./vagrant_insecure_key"
        ssh="ssh -p 20022 -q $ssh_opts vagrant@localhost"

        echo "Waiting for SSH..."
        for ((i = 0; i < 120; i++)); do
          echo "[ssh] Trying to connect..."
          if $ssh -- true; then
            echo "[ssh] Connected!"
            break
          fi
          if ! kill -0 $qemu_pid; then
            echo "qemu died unexpectedly"
            exit 1
          fi
          sleep 1
        done

        if [[ -n $postBoot ]]; then
          echo "Running post-boot commands..."
          $ssh "set -ex; $postBoot"
        fi

        echo "Copying installer..."
        scp -P 20022 $ssh_opts $binaryTarball/nix-*.tar.xz vagrant@localhost:nix.tar.xz

        echo "Running installer..."
        $ssh "set -eux; $installScript"

        echo "Copying the Nix tests..."
        rsync -av -e "ssh -p 20022 $ssh_opts" $nixTests/ vagrant@localhost:nixTests/

        rsync -av -e "ssh -p 20022 $ssh_opts" ${pkgsStatic.busybox}/ vagrant@localhost:bootstrapTools/
        rsync -av -e "ssh -p 20022 $ssh_opts" ${pkgsStatic.jq}/ vagrant@localhost:jq/
        rsync -av -e "ssh -p 20022 $ssh_opts" ${pkgsStatic.bash}/ vagrant@localhost:bash/

        echo "Testing Nix installation..."
        $ssh <<EOF
          set -ex

          # FIXME: get rid of this; ideally ssh should just work.
          source ~/.bash_profile || true
          source ~/.bash_login || true
          source ~/.profile || true
          source /etc/bashrc || true
          export PATH

          mkdir -p "\$HOME/.config/nix"
          printf 'substitute = false\ndownload-attempts = 0\nconnect-timeout = 1\nexperimental-features = nix-command flakes' > "\$HOME/.config/nix/nix.conf"

          chmod -R +rw nixTests

          cd nixTests
          sed \
            -e "s|@bash@|''$\{/home/vagrant/bash/bin/bash\}|" \
            -e 's|@coreutils@|''$\{/home/vagrant/bootstrapTools\}/bin|' \
            -e "s|@system@|${image.system}|" \
            config.nix.in > config.nix

          export PATH="\$PATH:/home/vagrant/jq/bin"

          run() {
            pushd "\$(dirname "\$1")"
            bash -euo pipefail "\$(basename "\$1")"
            rm -f result*
            popd
          }
          # We only run parts of the test suite which succeed on a "real" store
          run add.sh
          run brotli.sh
          run build-delete.sh
          run build-dry.sh
          run build.sh
          # run eval.sh
          # run export.sh
          run hash.sh
          run lang.sh
          run legacy-ssh-store.sh
          # run logging.sh
          run nar-access.sh
          # run nix-build.sh
          run nix-copy-ssh.sh
          run nix_path.sh
          run output-normalization.sh
          run pass-as-file.sh
          run path-from-hash-part.sh
          run placeholders.sh
          run search.sh
          run tarball.sh
          run why-depends.sh
          run zstd.sh
        EOF

        echo "Done!"
        touch $out
      '';

in

builtins.mapAttrs (imageName: image:
  { ${image.system} = builtins.mapAttrs (testName: test:
      makeTest imageName testName
    ) installScripts;
  }
) images
