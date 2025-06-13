# Test the container built by ../../docker.nix.

{
  config,
  ...
}:

let
  pkgs = config.nodes.machine.nixpkgs.pkgs;

  nixImage = pkgs.callPackage ../../docker.nix { };
  nixUserImage = pkgs.callPackage ../../docker.nix {
    name = "nix-user";
    uid = 1000;
    gid = 1000;
    uname = "user";
    gname = "user";
  };

  containerTestScript = ./nix-docker-test.sh;

in
{
  name = "nix-docker";

  nodes = {
    machine =
      {
        config,
        lib,
        pkgs,
        ...
      }:
      {
        virtualisation.diskSize = 4096;
      };
    cache =
      {
        config,
        lib,
        pkgs,
        ...
      }:
      {
        virtualisation.additionalPaths = [
          pkgs.stdenv
          pkgs.hello
        ];
        services.harmonia.enable = true;
        networking.firewall.allowedTCPPorts = [ 5000 ];
      };
  };

  testScript =
    { nodes }:
    ''
      cache.wait_for_unit("harmonia.service")
      cache.wait_for_unit("network-addresses-eth1.service")

      machine.succeed("mkdir -p /etc/containers")
      machine.succeed("""echo '{"default":[{"type":"insecureAcceptAnything"}]}' > /etc/containers/policy.json""")

      machine.succeed("${pkgs.podman}/bin/podman load -i ${nixImage}")
      machine.succeed("${pkgs.podman}/bin/podman run --rm nix nix --version")
      machine.succeed("${pkgs.podman}/bin/podman run --rm -i nix < ${containerTestScript}")

      machine.succeed("${pkgs.podman}/bin/podman load -i ${nixUserImage}")
      machine.succeed("${pkgs.podman}/bin/podman run --rm nix-user nix --version")
      machine.succeed("${pkgs.podman}/bin/podman run --rm -i nix-user < ${containerTestScript}")
      machine.succeed("[[ $(${pkgs.podman}/bin/podman run --rm nix-user stat -c %u /nix/store) = 1000 ]]")
    '';
}
