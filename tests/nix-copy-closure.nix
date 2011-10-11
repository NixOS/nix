# Test ‘nix-copy-closure’.

{ nixpkgs, nixos, system, nix }:

with import "${nixos}/lib/testing.nix" { inherit nixpkgs system; };

makeTest ({ pkgs, ... }: let pkgA = pkgs.aterm; pkgB = pkgs.wget; in {

  nodes =
    { client =
        { config, pkgs, ... }:
        { virtualisation.writableStore = true;
          virtualisation.pathsInNixDB = [ pkgA ];
          environment.nix = nix;
        };
        
      server =
        { config, pkgs, ... }:
        { services.openssh.enable = true;
          virtualisation.writableStore = true;
          virtualisation.pathsInNixDB = [ pkgB ];
          environment.nix = nix;
        };        
    };

  testScript = { nodes }:
    ''
      startAll;

      # Create an SSH key on the client.
      my $key = `${pkgs.openssh}/bin/ssh-keygen -t dsa -f key -N ""`;
      $client->succeed("mkdir -m 700 /root/.ssh");
      $client->copyFileFromHost("key", "/root/.ssh/id_dsa");
      $client->succeed("chmod 600 /root/.ssh/id_dsa");

      # Install the SSH key on the server.
      $server->succeed("mkdir -m 700 /root/.ssh");
      $server->copyFileFromHost("key.pub", "/root/.ssh/authorized_keys");
      $server->waitForJob("sshd");
      $client->succeed("ssh -o StrictHostKeyChecking=no " . $server->name() . " 'echo hello world'");

      # Copy the closure of package A from the client to the server.
      $server->fail("nix-store --check-validity ${pkgA}");
      $client->succeed("nix-copy-closure --to server --gzip ${pkgA} >&2");
      $server->succeed("nix-store --check-validity ${pkgA}");

      # Copy the closure of package B from the server to the client.
      $client->fail("nix-store --check-validity ${pkgB}");
      $client->succeed("nix-copy-closure --from server --gzip ${pkgB} >&2");
      $client->succeed("nix-store --check-validity ${pkgB}");
    '';

})
