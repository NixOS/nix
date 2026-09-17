{
  lib,
  binaryTarballs,
  nixpkgsFor,
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

    install-both-profile-links = {
      script = ''
        tar -xf ./nix.tar.xz
        mv ./nix-* nix
        ln -s $HOME/.local/state/nix/profiles/a-profile $HOME/.nix-profile
        mkdir -p $HOME/.local/state/nix
        ln -s $HOME/.local/state/nix/profiles/b-profile $HOME/.local/state/nix/profile
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

    # TODO: Also port over tests from NixOS/nix-installer as applicable and add smoke tests for
    # NixOS/nix-installer + refactor to support different installer flavors.
  };

  mockChannel =
    pkgs:
    pkgs.runCommand "mock-channel" { } ''
      mkdir nixexprs
      mkdir -p $out/channel
      echo -n 'someContent' > nixexprs/someFile
      tar cvf - nixexprs | bzip2 > $out/channel/nixexprs.tar.bz2
    '';

  disableSELinux = "sudo setenforce 0";

  images = {
    "ubuntu-22-04" = {
      "x86_64-linux" = {
        image = import <nix/fetchurl.nix> {
          url = "https://cloud-images.ubuntu.com/releases/jammy/release-20260913/ubuntu-22.04-server-cloudimg-amd64-disk-kvm.img";
          hash = "sha256-vicNXW2BZzkUpj6DjdgPo1xXGpXEQBoOU43RWiBxVyE=";
        };
      };
    };

    "ubuntu-24-04" = {
      "x86_64-linux" = {
        image = import <nix/fetchurl.nix> {
          url = "https://cloud-images.ubuntu.com/releases/noble/release-20260911/ubuntu-24.04-server-cloudimg-amd64.img";
          hash = "sha256-YSssDMG8QTpsuMOP1hF5TK8PK0NsUAE9izeU2xKtc1Q=";
        };
      };
    };

    "fedora-44" = {
      "x86_64-linux" = {
        image = import <nix/fetchurl.nix> {
          url = "https://download.fedoraproject.org/pub/fedora/linux/releases/44/Cloud/x86_64/images/Fedora-Cloud-Base-Generic-44-1.7.x86_64.qcow2";
          hash = "sha256-KGgP5bNxpaguv0OjGSbghqFo5ZlJ0DlpxQk+cHH5C38=";
        };
        postBoot = disableSELinux;
      };
    };

    "rocky-8" = {
      "x86_64-linux" = {
        image = import <nix/fetchurl.nix> {
          url = "https://dl.rockylinux.org/pub/rocky/8/images/x86_64/Rocky-8-GenericCloud-Base-8.10-20240528.0.x86_64.qcow2";
          hash = "sha256-5WBmxYYGGR6WGE3pqRg6OvM8Wby9h0DYsQygVKeonBQ=";
        };
        postBoot = disableSELinux;
      };
    };

    "rocky-9" = {
      "x86_64-linux" = {
        image = import <nix/fetchurl.nix> {
          url = "https://dl.rockylinux.org/pub/rocky/9/images/x86_64/Rocky-9-GenericCloud-Base-9.8-20260525.0.x86_64.qcow2";
          hash = "sha256-ksIGzG95DGFYMkfu/oeJD4goQgZiwXys8kfOx4q07sg=";
        };
        postBoot = disableSELinux;
        # Needs x86_64-v2
        extraQemuOpts = "-cpu Westmere-v2";
      };
    };

    "rocky-10" = {
      "x86_64-linux" = {
        image = import <nix/fetchurl.nix> {
          url = "https://dl.rockylinux.org/pub/rocky/10/images/x86_64/Rocky-10-GenericCloud-Base-10.2-20260525.0.x86_64.qcow2";
          hash = "sha256-n8np/xaIi7aKw5sDkuJcnJJoTVDIXxzOarVJNju8S0g=";
        };
        postBoot = disableSELinux;
        # Needs x86_64-v3
        extraQemuOpts = "-cpu Haswell-v1";
      };
    };
  };

  makeTest =
    {
      imageName,
      testName,
      system,
    }:
    let
      image = images.${imageName}.${system};
    in
    with nixpkgsFor.${system}.native;
    runCommand "installer-test-${imageName}-${testName}"
      {
        buildInputs = [
          qemu_kvm
          openssh
          cdrkit
        ];
        image = image.image;
        postBoot = image.postBoot or "";
        installScript = installScripts.${testName}.script;
        binaryTarball = binaryTarballs.${system};
      }
      ''
        shopt -s nullglob

        image_type=$(qemu-img info $image | sed 's/file format: \(.*\)/\1/; t; d')
        qemu-img create -b $image -F "$image_type" -f qcow2 ./disk.qcow2
        ssh-keygen -t ed25519 -f ./id_test

        # Configure our test user via cloud-init to get passwordless sudo and
        # the freshly generated ssh key.
        touch network-config
        touch meta-data
        cat << EOF > user-data
        #cloud-config
        users:
        - name: user
          shell: /bin/bash
          sudo: ALL=(ALL) NOPASSWD:ALL
          lock_passwd: true
          ssh_authorized_keys:
          - $(cat ./id_test.pub)
        EOF

        genisoimage -output seed.img -volid cidata -rational-rock -joliet user-data meta-data network-config
        extra_qemu_opts="${image.extraQemuOpts or ""}"
        ssh_port=20022

        echo "Starting qemu..."

        qemu-kvm -m 4096 -nographic \
          -drive id=disk1,file=./disk.qcow2,if=virtio \
          -drive file=./seed.img,media=cdrom \
          -netdev user,id=net0,restrict=yes,hostfwd=tcp::$ssh_port-:22 -device virtio-net-pci,netdev=net0 \
          -run-with exit-with-parent=on \
          $extra_qemu_opts &

        qemu_pid=$!

        ssh_opts="-o StrictHostKeyChecking=no -i ./id_test"
        ssh="ssh -p $ssh_port -q $ssh_opts user@localhost"

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
        scp -P $ssh_port $ssh_opts $binaryTarball/nix-*.tar.xz user@localhost:nix.tar.xz

        echo "Running installer..."
        $ssh "set -eux; $installScript"

        echo "Copying the mock channel"
        ssh -p $ssh_port $ssh_opts user@localhost "mkdir channel"
        scp -P $ssh_port $ssh_opts ${mockChannel pkgs}/channel/nixexprs.tar.bz2 user@localhost:channel/

        echo "Testing Nix installation..."
        $ssh <<EOF
          set -ex

          nix-env --version
          nix --extra-experimental-features nix-command store info

          out=\$(nix-build --no-substitute -E 'derivation { name = "foo"; system = "${system}"; builder = "/bin/sh"; args = ["-c" "echo foobar > \$out"]; }')
          [[ \$(cat \$out) = foobar ]]

          if pgrep nix-daemon; then
            MAYBESUDO="sudo --preserve-env=NIX_CONFIG"
          else
            MAYBESUDO=""
          fi

          export NIX_CONFIG="substituters = "
          $MAYBESUDO \$(which nix-channel) --add file://\$HOME/channel myChannel
          $MAYBESUDO \$(which nix-channel) --update
          [[ \$(nix-instantiate --eval --expr 'builtins.readFile <myChannel/someFile>') = '"someContent"' ]]
        EOF

        echo "Done!"
        touch $out
      '';

in

builtins.mapAttrs (
  imageName: imageForSystems:
  lib.concatMapAttrs (system: image: {
    ${system} = builtins.mapAttrs (
      testName: test: makeTest { inherit imageName testName system; }
    ) installScripts;
  }) imageForSystems
) images
