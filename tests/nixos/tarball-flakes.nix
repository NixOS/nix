{
  lib,
  config,
  nixpkgs,
  ...
}:

let
  pkgs = config.nodes.machine.nixpkgs.pkgs;

  root = pkgs.runCommand "nixpkgs-flake" { } ''
    mkdir -p $out/{stable,tags}

    set -x
    dir=nixpkgs-${nixpkgs.shortRev}
    cp -rd --preserve=ownership,timestamps ${nixpkgs} $dir
    # Set the correct timestamp in the tarball.
    find $dir -print0 | xargs -0 touch -h -t ${builtins.substring 0 12 nixpkgs.lastModifiedDate}.${
      builtins.substring 12 2 nixpkgs.lastModifiedDate
    } --
    tar cfz $out/stable/${nixpkgs.rev}.tar.gz $dir --hard-dereference

    # Set the "Link" header on the redirect but not the final response to
    # simulate an S3-like serving environment where the final host cannot set
    # arbitrary headers.
    cat >$out/tags/.htaccess <<EOF
    Redirect "/tags/latest.tar.gz" "/stable/${nixpkgs.rev}.tar.gz"
    Header always set Link "<http://localhost/stable/${nixpkgs.rev}.tar.gz?rev=${nixpkgs.rev}&revCount=1234>; rel=\"immutable\""
    EOF
  '';
in

{
  name = "tarball-flakes";

  nodes = {
    machine =
      { config, pkgs, ... }:
      {
        networking.firewall.allowedTCPPorts = [ 80 ];

        services.httpd.enable = true;
        services.httpd.adminAddr = "foo@example.org";
        services.httpd.extraConfig = ''
          ErrorLog syslog:local6
        '';
        services.httpd.virtualHosts."localhost" = {
          servedDirs = [
            {
              urlPath = "/";
              dir = root;
            }
          ];
        };

        virtualisation.writableStore = true;
        virtualisation.diskSize = 2048;
        virtualisation.additionalPaths = [
          pkgs.hello
          pkgs.fuse
        ];
        virtualisation.memorySize = 4096;
        nix.settings.substituters = lib.mkForce [ ];
        nix.extraOptions = "experimental-features = nix-command flakes";
      };
  };

  testScript =
    { nodes }:
    ''
      # fmt: off
      import json

      start_all()

      machine.wait_for_unit("httpd.service")

      out = machine.succeed("nix flake metadata --json http://localhost/tags/latest.tar.gz")
      print(out)
      info = json.loads(out)

      # Check that we got redirected to the immutable URL.
      assert info["locked"]["url"] == "http://localhost/stable/${nixpkgs.rev}.tar.gz"

      # Check that we got a fingerprint for caching.
      assert info["fingerprint"]

      # Check that we got the rev and revCount attributes.
      assert info["revision"] == "${nixpkgs.rev}"
      assert info["revCount"] == 1234

      # Check that a 0-byte HTTP 304 "Not modified" result works.
      machine.succeed("nix flake metadata --refresh --json http://localhost/tags/latest.tar.gz")

      # Check that fetching with rev/revCount/narHash succeeds.
      machine.succeed("nix flake metadata --json http://localhost/tags/latest.tar.gz?rev=" + info["revision"])
      machine.succeed("nix flake metadata --json http://localhost/tags/latest.tar.gz?revCount=" + str(info["revCount"]))
      machine.succeed("nix flake metadata --json http://localhost/tags/latest.tar.gz?narHash=" + info["locked"]["narHash"])

      # Check that fetching fails if we provide incorrect attributes.
      machine.fail("nix flake metadata --json http://localhost/tags/latest.tar.gz?rev=493300eb13ae6fb387fbd47bf54a85915acc31c0")
      machine.fail("nix flake metadata --json http://localhost/tags/latest.tar.gz?narHash=sha256-tbudgBSg+bHWHiHnlteNzN8TUvI80ygS9IULh4rklEw=")
    '';

}
