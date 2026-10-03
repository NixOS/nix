{
  lib,
  binaryTarballs,
  nixpkgsFor,
}:

let
  commonCheck = ''
    export NIX_CONFIG="substituters = "

    nix-env --version

    nix --extra-experimental-features nix-command store info
    nix-store --verify --check-contents
    nix store verify --all --extra-experimental-features nix-command --no-trust

    nix-channel --add file://$HOME/channel myChannel
    nix-channel --update
    [[ $(nix-instantiate --eval --expr 'builtins.readFile <myChannel/someFile>') = '"someContent"' ]]

    bad_mtime=$( find /nix/store/ -mindepth 1 ! -path /nix/store/.links \
                 -exec sh -c '[ "$(stat -c %Y "{}")" -ne 1 ]' \; -print -quit )
    if [ -n "$bad_mtime" ]; then
      echo "bad filesystem object mtime after install:"
      stat "$bad_mtime"
      exit 1
    fi
  '';

  installCases = {
    install-default = {
      install = ''
        tar -xf ./nix.tar.xz
        mv ./nix-* nix
        ./nix/install --no-channel-add
      '';
    };

    install-both-profile-links = {
      install = ''
        tar -xf ./nix.tar.xz
        mv ./nix-* nix
        ln -s $HOME/.local/state/nix/profiles/a-profile $HOME/.nix-profile
        mkdir -p $HOME/.local/state/nix
        ln -s $HOME/.local/state/nix/profiles/b-profile $HOME/.local/state/nix/profile
        ./nix/install --no-channel-add
      '';
    };

    install-force-no-daemon = {
      install = ''
        tar -xf ./nix.tar.xz
        mv ./nix-* nix
        ./nix/install --no-daemon --no-channel-add
      '';
    };

    install-force-daemon = {
      install = ''
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
    # Images are named such that the DrvName logic that extracts the derivation
    # name for logs doesn't treat the everything after the `-` as the version.
    # That's accomplished by adding `v` after the dash. This makes logs more
    # legible.

    "ubuntu-v22_04" = {
      "x86_64-linux" = {
        image = import <nix/fetchurl.nix> {
          url = "https://cloud-images.ubuntu.com/releases/jammy/release-20260913/ubuntu-22.04-server-cloudimg-amd64-disk-kvm.img";
          hash = "sha256-vicNXW2BZzkUpj6DjdgPo1xXGpXEQBoOU43RWiBxVyE=";
        };
      };
    };

    "ubuntu-v24_04" = {
      "x86_64-linux" = {
        image = import <nix/fetchurl.nix> {
          url = "https://cloud-images.ubuntu.com/releases/noble/release-20260911/ubuntu-24.04-server-cloudimg-amd64.img";
          hash = "sha256-YSssDMG8QTpsuMOP1hF5TK8PK0NsUAE9izeU2xKtc1Q=";
        };
      };
    };

    "fedora-v44" = {
      "x86_64-linux" = {
        image = import <nix/fetchurl.nix> {
          url = "https://download.fedoraproject.org/pub/fedora/linux/releases/44/Cloud/x86_64/images/Fedora-Cloud-Base-Generic-44-1.7.x86_64.qcow2";
          hash = "sha256-KGgP5bNxpaguv0OjGSbghqFo5ZlJ0DlpxQk+cHH5C38=";
        };
        postBoot = disableSELinux;
      };
    };

    "rocky-v8" = {
      "x86_64-linux" = {
        image = import <nix/fetchurl.nix> {
          url = "https://dl.rockylinux.org/pub/rocky/8/images/x86_64/Rocky-8-GenericCloud-Base-8.10-20240528.0.x86_64.qcow2";
          hash = "sha256-5WBmxYYGGR6WGE3pqRg6OvM8Wby9h0DYsQygVKeonBQ=";
        };
        postBoot = disableSELinux;
      };
    };

    "rocky-v9" = {
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

    "rocky-v10" = {
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

    # Docs on cloud-init quirks: https://gitlab.alpinelinux.org/alpine/aports/-/blob/master/community/cloud-init/README.Alpine?ref_type=heads
    "alpine-v3_23" = {
      "x86_64-linux" = {
        image = import <nix/fetchurl.nix> {
          url = "https://dl-cdn.alpinelinux.org/alpine/v3.23/releases/cloud/alpine-3.23.6-x86_64-bios-cloudinit-r0.qcow2";
          hash = "sha512-+F0E0lvjkmC273NNkY1+X2lpdf7uuuMuQtkBL6dmnWBX8gEZ0bq2fihWJIttnJaCTcaEMpT5IoSGrZ4IvBfVpg==";
        };
        shell = "/bin/sh";
        postBoot = "touch ~/.profile";
        installCases = {
          # Multi-user installer doesn't support non-bash shells or OpenRC.
          inherit (installCases) install-default install-both-profile-links install-force-no-daemon;
        };
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
      test = installCases.${testName};
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
        installScript = test.install;
        checkScript = commonCheck + (test.check or "");
        binaryTarball = binaryTarballs.${system};
      }
      ''
        shopt -s nullglob
        set -eu

        image_type=$(qemu-img info $image | sed 's/file format: \(.*\)/\1/; t; d')
        qemu-img create -b $image -F "$image_type" -f qcow2 ./disk.qcow2
        qemu-img resize ./disk.qcow2 +2G
        ssh-keygen -t ed25519 -f ./id_test

        # Configure our test user via cloud-init to get passwordless sudo and
        # the freshly generated ssh key.
        touch network-config
        touch meta-data
        cat << EOF > user-data
        #cloud-config
        users:
        - name: user
          shell: ${image.shell or "/bin/bash"}
          sudo: ALL=(ALL) NOPASSWD:ALL
          doas:
           - permit nopass user as root
          # Workaround for alpine that refuses ssh connections with locked accounts even with keys.
          hashed_passwd: '*'
          lock_passwd: false
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
          $ssh "set -eux; $postBoot"
        fi

        echo "Copying installer..."
        scp -P $ssh_port $ssh_opts $binaryTarball/nix-*.tar.xz user@localhost:nix.tar.xz

        echo "Running installer..."
        $ssh <<EOF
          set -eux

          # TODO: The installer should probably autodetect instead of requiring manually specifying this.
          if command -v sudo; then
            export NIX_BECOME=sudo
          elif command -v doas; then
            export NIX_BECOME=doas
          fi

          $installScript
        EOF

        echo "Copying the mock channel..."
        scp -r -P $ssh_port $ssh_opts ${mockChannel pkgs}/channel user@localhost:./

        echo "Testing Nix installation..."
        $ssh <<EOF
          set -eux
          $checkScript
        EOF

        echo "Done!"
        touch $out
      '';

in

builtins.mapAttrs (
  imageName: imageForSystems:
  lib.concatMapAttrs (system: image: {
    ${system} = builtins.mapAttrs (testName: test: makeTest { inherit imageName testName system; }) (
      image.installCases or installCases
    );
  }) imageForSystems
) images
