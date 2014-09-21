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
        system = "${system}";
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
        };
    };

  testScript = { nodes }:
    ''
      startAll;

      $client->waitForUnit("multi-user.target");

      # Root builds the derivation
      my $out = $client->succeed("nix-build ${expr nodes.client.config 1}");
      $out =~ s/\s+$//;
      my $outfile = $out . "/host";

      # Check that root can read & export the output of the derivation.
      $client->succeed("cat $outfile");
      $client->succeed("nix-store --export $out");

      # Check that alice cannot read / export the output of the derivation.
      $client->fail("su alice -c 'cat $outfile'");
      $client->fail("su alice -c 'nix-store --export $out'");
    '';
})
