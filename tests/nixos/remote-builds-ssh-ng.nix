test@{
  config,
  lib,
  hostPkgs,
  ...
}:

let
  pkgs = config.nodes.client.nixpkgs.pkgs;

  clientNixVersion = config.nodes.client.nix.package.version;
  clientSupportsDerivationMeta = lib.versionAtLeast clientNixVersion "2.35";

  builderNixVersion = config.nodes.builder.nix.package.version;
  builderSupportsDerivationMeta = lib.versionAtLeast builderNixVersion "2.35";

  # derivation-meta requires __structuredAttrs, which needs bash for .attrs.sh.
  metaExpr = pkgs.writeText "meta-expr.nix" ''
    let bash = builtins.storePath ${pkgs.bash}; in
    derivation {
      name = "meta-test";
      system = "i686-linux";
      builder = "''${bash}/bin/bash";
      args = [ "-c" "source $NIX_ATTRS_SH_FILE; echo hello > \''${outputs[out]}" ];
      __structuredAttrs = true;
      __meta = { description = "test"; };
      requiredSystemFeatures = [ "derivation-meta" ];
      outputs = [ "out" ];
    }
  '';

  # Trivial Nix expression to build remotely.
  expr =
    config: nr:
    pkgs.writeText "expr.nix" ''
      let utils = builtins.storePath ${config.system.build.extraUtils}; in
      derivation {
        name = "hello-${toString nr}";
        system = "i686-linux";
        PATH = "''${utils}/bin";
        builder = "''${utils}/bin/sh";
        args = [ "-c" "${
          lib.concatStringsSep "; " [
            "if [[ -n $NIX_LOG_FD ]]"
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

    nodes = {
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
        {
          config,
          lib,
          pkgs,
          ...
        }:
        {
          nix.settings.max-jobs = 0; # force remote building
          nix.settings.experimental-features = lib.optionals clientSupportsDerivationMeta [
            "derivation-meta"
          ];
          nix.distributedBuilds = true;
          nix.buildMachines = [
            {
              hostName = "builder";
              sshUser = "root";
              sshKey = "/root/.ssh/id_ed25519";
              system = "i686-linux";
              maxJobs = 1;
              protocol = "ssh-ng";
              supportedFeatures = lib.optionals clientSupportsDerivationMeta [ "derivation-meta" ];
            }
          ];
          virtualisation.writableStore = true;
          virtualisation.additionalPaths = [
            config.system.build.extraUtils
            pkgs.bash
          ];
          nix.settings.substituters = lib.mkForce [ ];
          programs.ssh.extraConfig = "ConnectTimeout 30";
        };
    };

    testScript =
      { nodes }:
      ''
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
        client.wait_for_unit("network-addresses-eth1.service")
        builder.succeed("mkdir -p -m 700 /root/.ssh")
        builder.copy_from_host("key.pub", "/root/.ssh/authorized_keys")
        builder.wait_for_unit("sshd")
        builder.wait_for_unit("multi-user.target")
        builder.wait_for_unit("network-addresses-eth1.service")

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
      ''
      + lib.optionalString clientSupportsDerivationMeta (
        if builderSupportsDerivationMeta then
          ''

            # Both client and builder support derivation-meta; building should work.
            client.succeed("nix-build ${metaExpr}")
          ''
        else
          ''

            # The client supports derivation-meta but the builder does not.
            # Expect a clear error instead of a confusing hash mismatch.
            output = client.fail("nix-build ${metaExpr} 2>&1")
            assert "'derivation-meta', but the store" in output, \
              f"Expected derivation-meta protocol feature error, got: {output}"
          ''
      );
  };
}
