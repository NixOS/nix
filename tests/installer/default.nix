{ binaryTarballs
, nixpkgsFor
}:

let

  installScripts = {
    install-default = {
      script = ''
        set -eux

        tar -xf ./nix.tar.xz
        mv ./nix-* nix
        ./nix/install --no-channel-add
      '';
    };

    install-force-no-daemon = {
      script = ''
        set -eux

        tar -xf ./nix.tar.xz
        mv ./nix-* nix
        ./nix/install --no-daemon
      '';
    };

    install-force-daemon = {
      script = ''
        set -eux

        tar -xf ./nix.tar.xz
        mv ./nix-* nix
        ./nix/install --daemon
      '';
    };
  };

  images = {

    "ubuntu-14-04" = {
      image = import <nix/fetchurl.nix> {
        url = https://app.vagrantup.com/ubuntu/boxes/trusty64/versions/20190514.0.0/providers/virtualbox.box;
        hash = "sha256-iUUXyRY8iW7DGirb0zwGgf1fRbLA7wimTJKgP7l/OQ8=";
      };
      rootDisk = "box-disk1.vmdk";
      system = "x86_64-linux";
    };

    "ubuntu-16-04" = {
      image = import <nix/fetchurl.nix> {
        url = https://app.vagrantup.com/ubuntu/boxes/xenial64/versions/20211001.0.0/providers/virtualbox.box;
        hash = "sha256-JCc0wd9vaSzCU8coByVtb/oDTAXYBPnORwEShS4oj4U=";
      };
      rootDisk = "ubuntu-xenial-16.04-cloudimg.vmdk";
      system = "x86_64-linux";
    };

    "ubuntu-22-10" = {
      image = import <nix/fetchurl.nix> {
        url = https://app.vagrantup.com/ubuntu/boxes/kinetic64/versions/20220910.0.0/providers/virtualbox.box;
        hash = "sha256-/IXr+Apyx2dqX6Gj4SoNtQ/5v1eKKopwzFgozAq6GFY=";
      };
      rootDisk = "ubuntu-kinetic-22.10-cloudimg.vmdk";
      system = "x86_64-linux";
    };


    "fedora-36" = {
      image = import <nix/fetchurl.nix> {
        url = https://app.vagrantup.com/generic/boxes/fedora36/versions/4.1.12/providers/libvirt.box;
        hash = "sha256-rxPgnDnFkTDwvdqn2CV3ZUo3re9AdPtSZ9SvOHNvaks=";
      };
      rootDisk = "box.img";
      system = "x86_64-linux";
    };

  };

  makeTest = imageName: testName:
    let image = images.${imageName}; in
    with nixpkgsFor.${image.system};
    runCommand
      "installer-test-${imageName}-${testName}"
      { buildInputs = [ qemu_kvm openssh ];
        image = image.image;
        installScript = installScripts.${testName}.script;
        binaryTarball = binaryTarballs.${system};
      }
      ''
        echo "Unpacking Vagrant box $image..."
        tar xvf $image

        image_type=$(qemu-img info ${image.rootDisk} | sed 's/file format: \(.*\)/\1/; t; d')

        qemu-img create -b ./${image.rootDisk} -F "$image_type" -f qcow2 ./disk.qcow2

        echo "Starting qemu..."
        qemu-kvm -m 4096 -nographic \
          -drive id=disk1,file=./disk.qcow2,if=virtio \
          -netdev user,id=net0,restrict=yes,hostfwd=tcp::20022-:22 -device virtio-net-pci,netdev=net0 &
        qemu_pid=$!
        trap "kill $qemu_pid" EXIT

        if ! [ -e ./vagrant_insecure_key ]; then
          cp ${./vagrant_insecure_key} vagrant_insecure_key
        fi

        chmod 0400 ./vagrant_insecure_key

        ssh_opts="-o StrictHostKeyChecking=no -o PubkeyAcceptedKeyTypes=+ssh-rsa -i ./vagrant_insecure_key"
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

        echo "Copying installer..."
        scp -P 20022 $ssh_opts $binaryTarball/nix-*.tar.xz vagrant@localhost:nix.tar.xz

        echo "Running installer..."
        $ssh "$installScript"

        echo "Testing Nix installation..."
        # FIXME: should update ~/.bashrc.
        $ssh "source ~/.bash_profile || source ~/.bash_login || source ~/.profile || true; nix-env --version"

        echo "Done!"
        touch $out
      '';

in

{
  ubuntu-14-04.install-default = makeTest "ubuntu-14-04" "install-default";
  #ubuntu-16-04.install-default = makeTest "ubuntu-16-04" "install-default";
  #ubuntu-22-10.install-default = makeTest "ubuntu-22-10" "install-default";
  fedora-36.install-default = makeTest "fedora-36" "install-default";
}
