{ nixpkgs, system, overlay }:

with import (nixpkgs + "/nixos/lib/testing.nix") {
  inherit system;
  extraConfigurations = [ { nixpkgs.overlays = [ overlay ]; } ];
};

let

  # Generate a fake root CA and a fake github.com certificate.
  cert = pkgs.runCommand "cert" { buildInputs = [ pkgs.openssl ]; }
    ''
      mkdir -p $out

      openssl genrsa -out ca.key 2048
      openssl req -new -x509 -days 36500 -key ca.key \
        -subj "/C=NL/ST=Denial/L=Springfield/O=Dis/CN=Root CA" -out $out/ca.crt

      openssl req -newkey rsa:2048 -nodes -keyout $out/server.key \
        -subj "/C=CN/ST=Denial/L=Springfield/O=Dis/CN=github.com" -out server.csr
      openssl x509 -req -extfile <(printf "subjectAltName=DNS:api.github.com,DNS:github.com,DNS:raw.githubusercontent.com") \
        -days 36500 -in server.csr -CA $out/ca.crt -CAkey ca.key -CAcreateserial -out $out/server.crt
    '';

  registry = pkgs.writeTextFile {
    name = "registry";
    text = ''
      {
        "flakes": [
          {
            "from": {
              "type": "indirect",
              "id": "nixpkgs"
            },
            "to": {
              "type": "github",
              "owner": "NixOS",
              "repo": "nixpkgs"
            }
          }
        ],
        "version": 2
      }
    '';
    destination = "/flake-registry.json";
  };

  api = pkgs.runCommand "nixpkgs-flake" {}
    ''
      mkdir -p $out/tarball

      dir=NixOS-nixpkgs-${nixpkgs.shortRev}
      cp -prd ${nixpkgs} $dir
      # Set the correct timestamp in the tarball.
      find $dir -print0 | xargs -0 touch -t ${builtins.substring 0 12 nixpkgs.lastModifiedDate}.${builtins.substring 12 2 nixpkgs.lastModifiedDate} --
      tar cfz $out/tarball/${nixpkgs.rev} $dir --hard-dereference

      mkdir -p $out/commits
      echo '{"sha": "${nixpkgs.rev}"}' > $out/commits/HEAD
    '';

in

makeTest (

{

  nodes =
    { # Impersonate github.com and api.github.com.
      github =
        { config, pkgs, ... }:
        { networking.firewall.allowedTCPPorts = [ 80 443 ];

          services.httpd.enable = true;
          services.httpd.adminAddr = "foo@example.org";
          services.httpd.extraConfig = ''
            ErrorLog syslog:local6
          '';
          services.httpd.virtualHosts."github.com" =
            { forceSSL = true;
              sslServerKey = "${cert}/server.key";
              sslServerCert = "${cert}/server.crt";
              servedDirs =
                [ { urlPath = "/NixOS/flake-registry/raw/master";
                    dir = registry;
                  }
                ];
            };
          services.httpd.virtualHosts."api.github.com" =
            { forceSSL = true;
              sslServerKey = "${cert}/server.key";
              sslServerCert = "${cert}/server.crt";
              servedDirs =
                [ { urlPath = "/repos/NixOS/nixpkgs";
                    dir = api;
                  }
                ];
            };
        };

      client =
        { config, lib, pkgs, nodes, ... }:
        { virtualisation.writableStore = true;
          virtualisation.diskSize = 2048;
          virtualisation.pathsInNixDB = [ pkgs.hello pkgs.fuse ];
          virtualisation.memorySize = 4096;
          nix.binaryCaches = lib.mkForce [ ];
          nix.extraOptions = "experimental-features = nix-command flakes";
          environment.systemPackages = [ pkgs.jq ];
          networking.hosts.${(builtins.head nodes.github.config.networking.interfaces.eth1.ipv4.addresses).address} =
            [ "github.com" "api.github.com" "raw.githubusercontent.com" ];
          security.pki.certificateFiles = [ "${cert}/ca.crt" ];
        };
    };

  testScript = { nodes }:
    ''
      use POSIX qw(strftime);

      startAll;

      $github->waitForUnit("httpd.service");

      $client->succeed("curl -v https://github.com/ >&2");

      $client->succeed("nix registry list | grep nixpkgs");

      $client->succeed("nix flake info nixpkgs --json | jq -r .revision") eq "${nixpkgs.rev}\n"
        or die "revision mismatch";

      $client->succeed("nix registry pin nixpkgs");

      $client->succeed("nix flake info nixpkgs --tarball-ttl 0 >&2");

      # Shut down the web server. The flake should be cached on the client.
      $github->succeed("systemctl stop httpd.service");

      my $date = $client->succeed("nix flake info nixpkgs --json | jq -M .lastModified");
      strftime("%Y%m%d%H%M%S", gmtime($date)) eq "${nixpkgs.lastModifiedDate}" or die "time mismatch";

      $client->succeed("nix build nixpkgs#hello");

      # The build shouldn't fail even with --tarball-ttl 0 (the server
      # being down should not be a fatal error).
      $client->succeed("nix build nixpkgs#fuse --tarball-ttl 0");
    '';

})
