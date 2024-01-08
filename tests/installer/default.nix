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

  makeTest = imageName: testName:
    let image = images.${imageName}; in
    with nixpkgsFor.${image.system}.native;
    runCommand
      "installer-test-${imageName}-${testName}"
      { buildInputs = [ qemu_kvm openssh ];
        image = image.image;
        postBoot = image.postBoot or "";
        installScript = installScripts.${testName}.script;
        binaryTarball = binaryTarballs.${system};
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

        echo "Copying the mock channel"
        # `scp -r` doesn't seem to work properly on some rhel instances, so let's
        # use a plain tarpipe instead
        tar -C ${mockChannel pkgs} -c channel | ssh -p 20022 $ssh_opts vagrant@localhost tar x -f-

        echo "Testing Nix installation..."
        $ssh <<EOF
          set -ex

          # FIXME: get rid of this; ideally ssh should just work.
          source ~/.bash_profile || true
          source ~/.bash_login || true
          source ~/.profile || true
          source /etc/bashrc || true

          nix-env --version
          nix --extra-experimental-features nix-command store info

          out=\$(nix-build --no-substitute -E 'derivation { name = "foo"; system = "x86_64-linux"; builder = "/bin/sh"; args = ["-c" "echo foobar > \$out"]; }')
          [[ \$(cat \$out) = foobar ]]

          if pgrep nix-daemon; then
            MAYBESUDO="sudo"
          else
            MAYBESUDO=""
          fi


          $MAYBESUDO \$(which nix-channel) --add file://\$HOME/channel myChannel
          $MAYBESUDO \$(which nix-channel) --update
          [[ \$(nix-instantiate --eval --expr 'builtins.readFile <myChannel/someFile>') = '"someContent"' ]]
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
