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
  expr = extraUtils: secret: nr: pkgs.writeText "expr.nix"
    ''
      let utils = builtins.storePath ${extraUtils}; in
      derivation {
        name = "hello-${toString nr}";
        system = "${system}";
        PATH = "''${utils}/bin";
        builder = "''${utils}/bin/sh";
        args = [ "-c" "mkdir $out; echo Hello-${toString nr} > $out/host" ];
        outputs = [ "out" ];
        secret = ${if secret then "true" else "false"};
      }
    '';

  # This is used to attempt fetching secret files from the store. On a single
  # user store with no build user, this will fail, but on a multiple user store
  # we should not be able to read any other file from the store, even if it is
  # referenced by the derivation.
  evilLog = extraUtils: pkgs.writeText "evil.nix" ''
    { path }:
    let utils = builtins.storePath ${extraUtils}; in
    derivation {
      name = "evil-dump-log";
      system = "${system}";
      PATH = "''${utils}/bin";
      builder = "''${utils}/bin/sh";
      args = [ "-c" "mkdir $out; cat ''${path}/host; echo 1 > $out/result" ];
    }
  '';

  evilOut = extraUtils: pkgs.writeText "evil.nix" ''
    { path }:
    let utils = builtins.storePath ${extraUtils}; in
    derivation {
      name = "hello-1";
      system = "${system}";
      PATH = "''${utils}/bin";
      builder = "''${utils}/bin/sh";
      args = [ "-c" "mkdir $out; cat ''${path}/host | tee $out/host" ];
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
          environment.systemPackages = [ pkgs.diffutils ];

          users.extraUsers.alice =
            { createHome = true;
              home = "/home/alice";
              description = "Alice Foobar";
              extraGroups = [ "wheel" ];
              useDefaultShell = true;
            };
        };
    };

  testScript = { nodes }:
    let extraUtils = nodes.client.config.system.build.extraUtils; in
    ''
      startAll;

      $client->waitForUnit("multi-user.target");

      # Root builds the derivation
      my $out = $client->succeed("nix-build ${expr extraUtils true 1}");
      $out =~ s/\s+$//;
      my $outfile = $out . "/host";

      # Check that root can read & export the output of the derivation.
      $client->succeed("cat $outfile");
      $client->succeed("nix-store --export $out");

      # Verify that alice can build a derivations
      $client->succeed("su alice -lc 'nix-build ${expr extraUtils false 2}'");

      # Check that alice cannot read / export the output of the derivation.
      $client->fail("su alice -lc 'cat $outfile'");
      $client->fail("su alice -lc 'nix-store --export $out'");

      # Verify that we cannot spew any sescret during a realization.
      $client->succeed("su alice -lc 'nix-build ${evilLog extraUtils} --argstr path $out | grep -v Hello-1'");

      # Check that we cannot copy the content to make it publicly visible.
      my $evilout = $client->succeed("su alice -lc 'nix-build ${evilOut extraUtils} --argstr path $out'");
      $evilout =~ s/\s+$//;
      $client->succeed("test $out != $evilout");
      my $eviloutfile = $evilout . "/host";
      $client->fail("diff $outfile $eviloutfile");
    '';
})
