# Test Nix's remote build feature.

{ lib, hostPkgs, ... }:

{
  config = {
    name = lib.mkDefault "git-submodules";

    nodes =
      {
        remote =
          { config, pkgs, ... }:
          {
            services.openssh.enable = true;
            environment.systemPackages = [ pkgs.git ];
          };

        client =
          { config, lib, pkgs, ... }:
          {
            programs.ssh.extraConfig = "ConnectTimeout 30";
            environment.systemPackages = [ pkgs.git ];
            nix.extraOptions = "experimental-features = nix-command flakes";
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

      # Install the SSH key on the builders.
      client.wait_for_unit("network.target")

      remote.succeed("mkdir -p -m 700 /root/.ssh")
      remote.copy_from_host("key.pub", "/root/.ssh/authorized_keys")
      remote.wait_for_unit("sshd")
      client.succeed(f"ssh -o StrictHostKeyChecking=no {remote.name} 'echo hello world'")

      remote.succeed("""
        git init bar
        git -C bar config user.email foobar@example.com
        git -C bar config user.name Foobar
        echo test >> bar/content
        git -C bar add content
        git -C bar commit -m 'Initial commit'
      """)

      client.succeed(f"""
        git init foo
        git -C foo config user.email foobar@example.com
        git -C foo config user.name Foobar
        git -C foo submodule add root@{remote.name}:/tmp/bar sub
        git -C foo add sub
        git -C foo commit -m 'Add submodule'
      """)

      client.succeed("nix --flake-registry \"\" flake prefetch 'git+file:///tmp/foo?submodules=1&ref=master'")
    '';
  };
}
