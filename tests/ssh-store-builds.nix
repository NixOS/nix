# Test Nix's remote build feature.

{ system, nix }:

with import <nixpkgs/nixos/lib/testing.nix> { inherit system; };

makeTest (

let

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

  nodes =
    { slave =
        { config, pkgs, ... }:
        { services.openssh.enable = true;
          virtualisation.writableStore = true;
          virtualisation.pathsInNixDB = [ config.system.build.extraUtils ];
          nix.package = nix;
          nix.binaryCaches = [ ];
          nix.useSandbox = true;
        };

      client =
        { config, pkgs, ... }:
        { environment.variables.NIX_REMOTE = "ssh-ng://root@slave?ssh-key=/root/.ssh/id_rsa";
          virtualisation.writableStore = true;
          nix.package = nix;
          nix.binaryCaches = [ ];
          programs.ssh.extraConfig = "ConnectTimeout 30";
        };
    };

  testScript = { nodes }:
    ''
      startAll;

      # Create an SSH key on the client.
      my $key = `${pkgs.openssh}/bin/ssh-keygen -t dsa -f key -N ""`;
      $client->succeed("mkdir -p -m 700 /root/.ssh");
      $client->copyFileFromHost("key", "/root/.ssh/id_dsa");
      $client->succeed("chmod 600 /root/.ssh/id_dsa");

      # Install the SSH key on the slaves.
      $client->waitForUnit("network.target");
      $slave->succeed("mkdir -p -m 700 /root/.ssh");
      $slave->copyFileFromHost("key.pub", "/root/.ssh/authorized_keys");
      $slave->waitForUnit("sshd");
      $client->succeed("ssh -o StrictHostKeyChecking=no " . $slave->name() . " 'echo hello world'");

      # Perform a build and check that it was performed on the slave.
      my $out = $client->succeed(
        "nix-build ${expr nodes.client.config 1} 2> build-output",
        "grep -q Hello build-output"
      );
      $slave->succeed("test -e $out");
      $client->succeed("! test -e $out");
    '';

})
