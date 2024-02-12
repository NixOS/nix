test@{ config, lib, hostPkgs, ... }:

let
  pkgs = config.nodes.client.nixpkgs.pkgs;

  # Trivial Nix expression to build remotely.
  expr = config: nr: pkgs.writeText "expr.nix"
    ''
      let utils = builtins.storePath ${config.system.build.extraUtils}; in
      derivation {
        name = "hello-${toString nr}";
        system = "i686-linux";
        PATH = "''${utils}/bin";
        builder = "''${utils}/bin/sh";
        args = [ "-c" "${
          lib.concatStringsSep "; " [
            ''if [[ -n $NIX_LOG_FD ]]''
            ''then echo '@nix {\"action\":\"setPhase\",\"phase\":\"buildPhase\"}' >&''$NIX_LOG_FD''
            "fi"
            "echo Hello"
            "mkdir $out"
            "cat /proc/sys/kernel/hostname > $out/host"
          ]
        }" ];
        outputs = [ "out" ];
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
    name = lib.mkDefault "remote-builds-ssh-ng";

    nodes =
      {
        builder =
          { config, pkgs, ... }:
          {
            imports = [ test.config.builders.config ];
            services.openssh.enable = true;
            virtualisation.writableStore = true;
            nix.settings.sandbox = true;
            nix.settings.substituters = lib.mkForce [ ];
          };

        client =
          { config, lib, pkgs, ... }:
          {
            nix.settings.max-jobs = 0; # force remote building
            nix.distributedBuilds = true;
            nix.buildMachines =
              [{
                hostName = "builder";
                sshUser = "root";
                sshKey = "/root/.ssh/id_ed25519";
                system = "i686-linux";
                maxJobs = 1;
                protocol = "ssh-ng";
              }];
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

      # Install the SSH key on the builder.
      client.wait_for_unit("network.target")
      builder.succeed("mkdir -p -m 700 /root/.ssh")
      builder.copy_from_host("key.pub", "/root/.ssh/authorized_keys")
      builder.wait_for_unit("sshd")
      client.succeed(f"ssh -o StrictHostKeyChecking=no {builder.name} 'echo hello world'")

      # Perform a build
      out = client.succeed("nix-build ${expr nodes.client 1} 2> build-output")

      # Verify that the build was done on the builder
      builder.succeed(f"test -e {out.strip()}")

      # Print the build log, prefix the log lines to avoid nix intercepting lines starting with @nix
      buildOutput = client.succeed("sed -e 's/^/build-output:/' build-output")
      print(buildOutput)

      # Make sure that we get the expected build output
      client.succeed("grep -qF Hello build-output")

      # We don't want phase reporting in the build output
      client.fail("grep -qF '@nix' build-output")

      # Get the log file
      client.succeed(f"nix-store --read-log {out.strip()} > log-output")
      # Prefix the log lines to avoid nix intercepting lines starting with @nix
      logOutput = client.succeed("sed -e 's/^/log-file:/' log-output")
      print(logOutput)

      # Check that we get phase reporting in the log file
      client.succeed("grep -q '@nix {\"action\":\"setPhase\",\"phase\":\"buildPhase\"}' log-output")
    '';
  };
}
