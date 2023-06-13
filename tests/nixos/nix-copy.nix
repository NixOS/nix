# Test that ‘nix copy’ works over ssh.

{ lib, config, nixpkgs, hostPkgs, ... }:

let
  pkgs = config.nodes.client.nixpkgs.pkgs;

  pkgA = pkgs.cowsay;
  pkgB = pkgs.wget;
  pkgC = pkgs.hello;
  pkgD = pkgs.tmux;

in {
  name = "nix-copy";

  enableOCR = true;

  nodes =
    { client =
        { config, lib, pkgs, ... }:
        { virtualisation.writableStore = true;
          virtualisation.additionalPaths = [ pkgA pkgD.drvPath ];
          nix.settings.substituters = lib.mkForce [ ];
          nix.settings.experimental-features = [ "nix-command" ];
          services.getty.autologinUser = "root";
          programs.ssh.extraConfig = ''
            Host *
                ControlMaster auto
                ControlPath ~/.ssh/master-%h:%r@%n:%p
                ControlPersist 15m
          '';
        };

      server =
        { config, pkgs, ... }:
        { services.openssh.enable = true;
          services.openssh.permitRootLogin = "yes";
					users.users.root.password = "foobar";
          virtualisation.writableStore = true;
          virtualisation.additionalPaths = [ pkgB pkgC ];
        };
    };

  testScript = { nodes }: ''
    # fmt: off
    import subprocess

    # Create an SSH key on the client.
    subprocess.run([
      "${pkgs.openssh}/bin/ssh-keygen", "-t", "ed25519", "-f", "key", "-N", ""
    ], capture_output=True, check=True)

    start_all()

    server.wait_for_unit("sshd")
    client.wait_for_unit("network.target")
    client.wait_for_unit("getty@tty1.service")
    client.wait_for_text("]#")

    # Copy the closure of package A from the client to the server using password authentication,
    # and check that all prompts are visible
    server.fail("nix-store --check-validity ${pkgA}")
    client.send_chars("nix copy --to ssh://server ${pkgA} >&2; echo done\n")
    client.wait_for_text("continue connecting")
    client.send_chars("yes\n")
    client.wait_for_text("Password:")
    client.send_chars("foobar\n")
    client.wait_for_text("done")
    server.succeed("nix-store --check-validity ${pkgA}")

    # Check that ControlMaster is working
    client.send_chars("nix copy --to ssh://server ${pkgA} >&2; echo done\n")
    client.wait_for_text("done")

    client.copy_from_host("key", "/root/.ssh/id_ed25519")
    client.succeed("chmod 600 /root/.ssh/id_ed25519")

    # Install the SSH key on the server.
    server.copy_from_host("key.pub", "/root/.ssh/authorized_keys")
    server.succeed("systemctl restart sshd")
    client.succeed(f"ssh -o StrictHostKeyChecking=no {server.name} 'echo hello world'")
    client.succeed(f"ssh -O check {server.name}")
    client.succeed(f"ssh -O exit {server.name}")
    client.fail(f"ssh -O check {server.name}")

    # Check that an explicit master will work
    client.succeed(f"ssh -MNfS /tmp/master {server.name}")
    client.succeed(f"ssh -S /tmp/master -O check {server.name}")
    client.succeed("NIX_SSHOPTS='-oControlPath=/tmp/master' nix copy --to ssh://server ${pkgA} >&2")
    client.succeed(f"ssh -S /tmp/master -O exit {server.name}")

    # Copy the closure of package B from the server to the client, using ssh-ng.
    client.fail("nix-store --check-validity ${pkgB}")
    # Shouldn't download untrusted paths by default
    client.fail("nix copy --from ssh-ng://server ${pkgB} >&2")
    client.succeed("nix copy --no-check-sigs --from ssh-ng://server ${pkgB} >&2")
    client.succeed("nix-store --check-validity ${pkgB}")

    # Copy the derivation of package D's derivation from the client to the server.
    server.fail("nix-store --check-validity ${pkgD.drvPath}")
    client.succeed("nix copy --derivation --to ssh://server ${pkgD.drvPath} >&2")
    server.succeed("nix-store --check-validity ${pkgD.drvPath}")
  '';
}
