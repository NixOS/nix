# Test Nix's remote build feature.

{ system, nix }:

with import <nixpkgs/nixos/lib/testing.nix> { inherit system; };

makeTest (

let

  # The configuration of the build slaves.
  slave =
    { config, pkgs, ... }:
    { services.openssh.enable = true;
      virtualisation.writableStore = true;
      environment.nix = nix;
    };

  # Trivial Nix expression to build remotely.
  expr = config: nr: pkgs.writeText "expr.nix"
    ''
      let utils = builtins.storePath ${config.system.build.extraUtils}; in
      derivation {
        name = "hello-${toString nr}";
        inherit system;
        PATH = "''${utils}/bin";
        builder = "''${utils}/bin/sh";
        args = [ "-c" "mkdir $out; echo Hello-${toString nr} > $out/host" ];
        outputs = [ "out" ];
        secret = true;
      }
    '';

in

{

  nodes =
    { client =
        { config, pkgs, ... }:
        { virtualisation.writableStore = true;
          virtualisation.pathsInNixDB = [ config.system.build.extraUtils ];
          nix.package = nix;
          nix.binaryCaches = [ ];

          users.extraUsers.alice =
            { createHome = true;
              home = "/home/alice";
              description = "Alice Foobar";
              extraGroups = [ "wheel" ];
            };

          users.extraUsers.bob =
            { createHome = true;
              home = "/home/bob";
              description = "Bob Foobar";
              extraGroups = [ "wheel" ];
            };
        };
    };

  testScript = { nodes }:
    ''
      startAll;

      $client->waitForFile("/home/alice");
      $client->waitForUnit("nix-daemon.service");

      # Perform a build and check that it was performed on the slave.
      my $out = $client->succeed("su alice -c 'nix-build ${expr nodes.client.config 1}'");
      client->succeed("test -e $out");

      $client->succeed("su alice -c 'nix-store --export $out'");

      $client->waitForFile("/home/bob");
      $client->fail("su bob -c 'nix-store --export $out'");
    '';

})
