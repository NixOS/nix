# Test Nix's remote build feature.

test@{ config, lib, hostPkgs, ... }:

let
  pkgs = config.nodes.client.nixpkgs.pkgs;

  # The configuration of the remote builders.
  builder =
    { config, pkgs, ... }:
    {
      imports = [ test.config.builders.config ];
      services.openssh.enable = true;
      virtualisation.writableStore = true;
      nix.settings.sandbox = true;

      # Regression test for use of PID namespaces when /proc has
      # filesystems mounted on top of it
      # (i.e. /proc/sys/fs/binfmt_misc).
      boot.binfmt.emulatedSystems = [ "aarch64-linux" ];
    };

  # Trivial Nix expression to build remotely.
  expr = config: nr: pkgs.writeText "expr.nix"
    ''
      let utils = builtins.storePath ${config.system.build.extraUtils}; in
      derivation {
        name = "hello-${toString nr}";
        system = "i686-linux";
        PATH = "''${utils}/bin";
        builder = "''${utils}/bin/sh";
        args = [ "-c" "if [ ${toString nr} = 5 ]; then echo FAIL; exit 1; fi; echo Hello; mkdir $out $foo; cat /proc/sys/kernel/hostname > $out/host; ln -s $out $foo/bar; sleep 10" ];
        outputs = [ "out" "foo" ];
      }
    '';

in

{
  options = {
    builders.config = lib.mkOption {
      type = lib.types.deferredModule;
      description = ''
        Configuration to add to the builder nodes.
      '';
      default = { };
    };
  };

  config = {
    name = lib.mkDefault "remote-builds";

    nodes =
      {
        builder1 = builder;
        builder2 = builder;

        client =
          { config, lib, pkgs, ... }:
          {
            nix.settings.max-jobs = 0; # force remote building
            nix.distributedBuilds = true;
            nix.buildMachines =
              [
                {
                  hostName = "builder1";
                  sshUser = "root";
                  sshKey = "/root/.ssh/id_ed25519";
                  system = "i686-linux";
                  maxJobs = 1;
                }
                {
                  hostName = "builder2";
                  sshUser = "root";
                  sshKey = "/root/.ssh/id_ed25519";
                  system = "i686-linux";
                  maxJobs = 1;
                }
              ];
            virtualisation.writableStore = true;
            virtualisation.additionalPaths = [ config.system.build.extraUtils ];
            nix.settings.substituters = lib.mkForce [ ];
            programs.ssh.extraConfig = "ConnectTimeout 30";
          };
      };

    testScript = { nodes }: ''
      # fmt: off
      import subprocess

      start_all()

      # Create an SSH key on the client.
      subprocess.run([
        "${hostPkgs.openssh}/bin/ssh-keygen", "-t", "ed25519", "-f", "key", "-N", ""
      ], capture_output=True, check=True)
      client.succeed("mkdir -p -m 700 /root/.ssh")
      client.copy_from_host("key", "/root/.ssh/id_ed25519")
      client.succeed("chmod 600 /root/.ssh/id_ed25519")

      # Install the SSH key on the builders.
      client.wait_for_unit("network.target")
      for builder in [builder1, builder2]:
        builder.succeed("mkdir -p -m 700 /root/.ssh")
        builder.copy_from_host("key.pub", "/root/.ssh/authorized_keys")
        builder.wait_for_unit("sshd")
        client.succeed(f"ssh -o StrictHostKeyChecking=no {builder.name} 'echo hello world'")

      # Perform a build and check that it was performed on the builder.
      out = client.succeed(
        "nix-build ${expr nodes.client 1} 2> build-output",
        "grep -q Hello build-output"
      )
      builder1.succeed(f"test -e {out}")

      # And a parallel build.
      paths = client.succeed(r'nix-store -r $(nix-instantiate ${expr nodes.client 2})\!out $(nix-instantiate ${expr nodes.client 3})\!out')
      out1, out2 = paths.split()
      builder1.succeed(f"test -e {out1} -o -e {out2}")
      builder2.succeed(f"test -e {out1} -o -e {out2}")

      # And a failing build.
      client.fail("nix-build ${expr nodes.client 5}")

      # Test whether the build hook automatically skips unavailable builders.
      builder1.block()
      client.succeed("nix-build ${expr nodes.client 4}")
    '';
  };
}
