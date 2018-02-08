# Test ‘nix-copy-closure’.

{ nixpkgs, system, nix }:

with import (nixpkgs + "/nixos/lib/testing.nix") { inherit system; };

makeTest (let pkgA = pkgs.cowsay; pkgB = pkgs.wget; pkgC = pkgs.hello; in {

  nodes =
    { client =
        { config, pkgs, ... }:
        { virtualisation.writableStore = true;
          virtualisation.pathsInNixDB = [ pkgA ];
          nix.package = nix;
          nix.binaryCaches = [ ];
        };

      server =
        { config, pkgs, ... }:
        { services.openssh.enable = true;
          virtualisation.writableStore = true;
          virtualisation.pathsInNixDB = [ pkgB pkgC ];
          nix.package = nix;
        };
    };

  testScript = { nodes }:
    ''
      startAll;

      # Create an SSH key on the client.
      my $key = `${pkgs.openssh}/bin/ssh-keygen -t ed25519 -f key -N ""`;
      $client->succeed("mkdir -m 700 /root/.ssh");
      $client->copyFileFromHost("key", "/root/.ssh/id_ed25519");
      $client->succeed("chmod 600 /root/.ssh/id_ed25519");

      # Install the SSH key on the server.
      $server->succeed("mkdir -m 700 /root/.ssh");
      $server->copyFileFromHost("key.pub", "/root/.ssh/authorized_keys");
      $server->waitForUnit("sshd");
      $client->waitForUnit("network.target");
      $client->succeed("ssh -o StrictHostKeyChecking=no " . $server->name() . " 'echo hello world'");

      # Copy the closure of package A from the client to the server.
      $server->fail("nix-store --check-validity ${pkgA}");
      $client->succeed("nix-copy-closure --to server --gzip ${pkgA} >&2");
      $server->succeed("nix-store --check-validity ${pkgA}");

      # Copy the closure of package B from the server to the client.
      $client->fail("nix-store --check-validity ${pkgB}");
      $client->succeed("nix-copy-closure --from server --gzip ${pkgB} >&2");
      $client->succeed("nix-store --check-validity ${pkgB}");

      # Copy the closure of package C via the SSH substituter.
      $client->fail("nix-store -r ${pkgC}");
      # FIXME
      #$client->succeed(
      #  "nix-store --option use-ssh-substituter true"
      #  . " --option ssh-substituter-hosts root\@server"
      #  . " -r ${pkgC} >&2");
      #$client->succeed("nix-store --check-validity ${pkgC}");
    '';

})
