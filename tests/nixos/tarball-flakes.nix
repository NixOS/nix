{ lib, config, nixpkgs, ... }:

let
  pkgs = config.nodes.machine.nixpkgs.pkgs;

  root = pkgs.runCommand "nixpkgs-flake" {}
    ''
      mkdir -p $out/stable

      set -x
      dir=nixpkgs-${nixpkgs.shortRev}
      cp -prd ${nixpkgs} $dir
      # Set the correct timestamp in the tarball.
      find $dir -print0 | xargs -0 touch -t ${builtins.substring 0 12 nixpkgs.lastModifiedDate}.${builtins.substring 12 2 nixpkgs.lastModifiedDate} --
      tar cfz $out/stable/${nixpkgs.rev}.tar.gz $dir --hard-dereference

      echo 'Redirect "/latest.tar.gz" "/stable/${nixpkgs.rev}.tar.gz"' > $out/.htaccess

      echo 'Header set Link "<http://localhost/stable/${nixpkgs.rev}.tar.gz?rev=${nixpkgs.rev}&revCount=1234>; rel=\"immutable\""' > $out/stable/.htaccess
    '';
in

{
  name = "tarball-flakes";

  nodes =
    {
      machine =
        { config, pkgs, ... }:
        { networking.firewall.allowedTCPPorts = [ 80 ];

          services.httpd.enable = true;
          services.httpd.adminAddr = "foo@example.org";
          services.httpd.extraConfig = ''
            ErrorLog syslog:local6
          '';
          services.httpd.virtualHosts."localhost" =
            { servedDirs =
                [ { urlPath = "/";
                    dir = root;
                  }
                ];
            };

          virtualisation.writableStore = true;
          virtualisation.diskSize = 2048;
          virtualisation.additionalPaths = [ pkgs.hello pkgs.fuse ];
          virtualisation.memorySize = 4096;
          nix.settings.substituters = lib.mkForce [ ];
          nix.extraOptions = "experimental-features = nix-command flakes";
        };
    };

  testScript = { nodes }: ''
    # fmt: off
    import json

    start_all()

    machine.wait_for_unit("httpd.service")

    out = machine.succeed("nix flake metadata --json http://localhost/latest.tar.gz")
    print(out)
    info = json.loads(out)

    # Check that we got redirected to the immutable URL.
    assert info["locked"]["url"] == "http://localhost/stable/${nixpkgs.rev}.tar.gz"

    # Check that we got the rev and revCount attributes.
    assert info["revision"] == "${nixpkgs.rev}"
    assert info["revCount"] == 1234

    # Check that fetching with rev/revCount/narHash succeeds.
    machine.succeed("nix flake metadata --json http://localhost/latest.tar.gz?rev=" + info["revision"])
    machine.succeed("nix flake metadata --json http://localhost/latest.tar.gz?revCount=" + str(info["revCount"]))
    machine.succeed("nix flake metadata --json http://localhost/latest.tar.gz?narHash=" + info["locked"]["narHash"])

    # Check that fetching fails if we provide incorrect attributes.
    machine.fail("nix flake metadata --json http://localhost/latest.tar.gz?rev=493300eb13ae6fb387fbd47bf54a85915acc31c0")
    machine.fail("nix flake metadata --json http://localhost/latest.tar.gz?revCount=789")
    machine.fail("nix flake metadata --json http://localhost/latest.tar.gz?narHash=sha256-tbudgBSg+bHWHiHnlteNzN8TUvI80ygS9IULh4rklEw=")
  '';

}
