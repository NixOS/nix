# Test Nix's remote build feature.

{ nixpkgs, system, nix }:

with import (nixpkgs + "/nixos/lib/testing.nix") { inherit system; };

makeTest (

let

  # The configuration of the remote builders.
  builder =
    { config, pkgs, ... }:
    { services.openssh.enable = true;
      virtualisation.writableStore = true;
      nix.package = nix;
      nix.useSandbox = true;
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

  nodes =
    { builder1 = builder;
      builder2 = builder;

      client =
        { config, pkgs, ... }:
        { nix.maxJobs = 0; # force remote building
          nix.distributedBuilds = true;
          nix.buildMachines =
            [ { hostName = "builder1";
                sshUser = "root";
                sshKey = "/root/.ssh/id_ed25519";
                system = "i686-linux";
                maxJobs = 1;
              }
              { hostName = "builder2";
                sshUser = "root";
                sshKey = "/root/.ssh/id_ed25519";
                system = "i686-linux";
                maxJobs = 1;
              }
            ];
          virtualisation.writableStore = true;
          virtualisation.pathsInNixDB = [ config.system.build.extraUtils ];
          nix.package = nix;
          nix.binaryCaches = [ ];
          programs.ssh.extraConfig = "ConnectTimeout 30";
        };
    };

  testScript = { nodes }:
    ''
      startAll;

      # Create an SSH key on the client.
      my $key = `${pkgs.openssh}/bin/ssh-keygen -t ed25519 -f key -N ""`;
      $client->succeed("mkdir -p -m 700 /root/.ssh");
      $client->copyFileFromHost("key", "/root/.ssh/id_ed25519");
      $client->succeed("chmod 600 /root/.ssh/id_ed25519");

      # Install the SSH key on the builders.
      $client->waitForUnit("network.target");
      foreach my $builder ($builder1, $builder2) {
          $builder->succeed("mkdir -p -m 700 /root/.ssh");
          $builder->copyFileFromHost("key.pub", "/root/.ssh/authorized_keys");
          $builder->waitForUnit("sshd");
          $client->succeed("ssh -o StrictHostKeyChecking=no " . $builder->name() . " 'echo hello world'");
      }

      # Perform a build and check that it was performed on the builder.
      my $out = $client->succeed(
        "nix-build ${expr nodes.client.config 1} 2> build-output",
        "grep -q Hello build-output"
      );
      $builder1->succeed("test -e $out");

      # And a parallel build.
      my ($out1, $out2) = split /\s/,
          $client->succeed('nix-store -r $(nix-instantiate ${expr nodes.client.config 2})\!out $(nix-instantiate ${expr nodes.client.config 3})\!out');
      $builder1->succeed("test -e $out1 -o -e $out2");
      $builder2->succeed("test -e $out1 -o -e $out2");

      # And a failing build.
      $client->fail("nix-build ${expr nodes.client.config 5}");

      # Test whether the build hook automatically skips unavailable builders.
      $builder1->block;
      $client->succeed("nix-build ${expr nodes.client.config 4}");
    '';

})
