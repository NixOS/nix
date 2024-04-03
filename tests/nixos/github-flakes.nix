{ lib, config, nixpkgs, ... }:
let
  pkgs = config.nodes.client.nixpkgs.pkgs;

  # Generate a fake root CA and a fake api.github.com / github.com / channels.nixos.org certificate.
  cert = pkgs.runCommand "cert" { nativeBuildInputs = [ pkgs.openssl ]; }
    ''
      mkdir -p $out

      openssl genrsa -out ca.key 2048
      openssl req -new -x509 -days 36500 -key ca.key \
        -subj "/C=NL/ST=Denial/L=Springfield/O=Dis/CN=Root CA" -out $out/ca.crt

      openssl req -newkey rsa:2048 -nodes -keyout $out/server.key \
        -subj "/C=CN/ST=Denial/L=Springfield/O=Dis/CN=github.com" -out server.csr
      openssl x509 -req -extfile <(printf "subjectAltName=DNS:api.github.com,DNS:github.com,DNS:channels.nixos.org") \
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
          },
          {
            "from": {
              "type": "indirect",
              "id": "private-flake"
            },
            "to": {
              "type": "github",
              "owner": "fancy-enterprise",
              "repo": "private-flake"
            }
          }
        ],
        "version": 2
      }
    '';
    destination = "/flake-registry.json";
  };

  private-flake-rev = "9f1dd0df5b54a7dc75b618034482ed42ce34383d";

  private-flake-api = pkgs.runCommand "private-flake" {}
    ''
      mkdir -p $out/{commits,tarball}

      # Setup https://docs.github.com/en/rest/commits/commits#get-a-commit
      echo '{"sha": "${private-flake-rev}", "commit": {"tree": {"sha": "ffffffffffffffffffffffffffffffffffffffff"}}}' > $out/commits/HEAD

      # Setup tarball download via API
      dir=private-flake
      mkdir $dir
      echo '{ outputs = {...}: {}; }' > $dir/flake.nix
      tar cfz $out/tarball/${private-flake-rev} $dir --hard-dereference
    '';

  nixpkgs-api = pkgs.runCommand "nixpkgs-flake" {}
    ''
      mkdir -p $out/commits

      # Setup https://docs.github.com/en/rest/commits/commits#get-a-commit
      echo '{"sha": "${nixpkgs.rev}", "commit": {"tree": {"sha": "ffffffffffffffffffffffffffffffffffffffff"}}}' > $out/commits/HEAD
    '';

  archive = pkgs.runCommand "nixpkgs-flake" {}
    ''
      mkdir -p $out/archive

      dir=NixOS-nixpkgs-${nixpkgs.shortRev}
      cp -prd ${nixpkgs} $dir
      # Set the correct timestamp in the tarball.
      find $dir -print0 | xargs -0 touch -h -t ${builtins.substring 0 12 nixpkgs.lastModifiedDate}.${builtins.substring 12 2 nixpkgs.lastModifiedDate} --
      tar cfz $out/archive/${nixpkgs.rev}.tar.gz $dir --hard-dereference
    '';
in

{
  name = "github-flakes";

  nodes =
    {
      github =
        { config, pkgs, ... }:
        { networking.firewall.allowedTCPPorts = [ 80 443 ];

          services.httpd.enable = true;
          services.httpd.adminAddr = "foo@example.org";
          services.httpd.extraConfig = ''
            ErrorLog syslog:local6
          '';
          services.httpd.virtualHosts."channels.nixos.org" =
            { forceSSL = true;
              sslServerKey = "${cert}/server.key";
              sslServerCert = "${cert}/server.crt";
              servedDirs =
                [ { urlPath = "/";
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
                    dir = nixpkgs-api;
                  }
                  { urlPath = "/repos/fancy-enterprise/private-flake";
                    dir = private-flake-api;
                  }
                ];
            };
          services.httpd.virtualHosts."github.com" =
            { forceSSL = true;
              sslServerKey = "${cert}/server.key";
              sslServerCert = "${cert}/server.crt";
              servedDirs =
                [ { urlPath = "/NixOS/nixpkgs";
                    dir = archive;
                  }
                ];
            };
        };

      client =
        { config, lib, pkgs, nodes, ... }:
        { virtualisation.writableStore = true;
          virtualisation.diskSize = 2048;
          virtualisation.additionalPaths = [ pkgs.hello pkgs.fuse ];
          virtualisation.memorySize = 4096;
          nix.settings.substituters = lib.mkForce [ ];
          nix.extraOptions = "experimental-features = nix-command flakes";
          networking.hosts.${(builtins.head nodes.github.networking.interfaces.eth1.ipv4.addresses).address} =
            [ "channels.nixos.org" "api.github.com" "github.com" ];
          security.pki.certificateFiles = [ "${cert}/ca.crt" ];
        };
    };

  testScript = { nodes }: ''
    # fmt: off
    import json
    import time

    start_all()

    def cat_log():
         github.succeed("cat /var/log/httpd/*.log >&2")

    github.wait_for_unit("httpd.service")

    client.succeed("curl -v https://github.com/ >&2")
    out = client.succeed("nix registry list")
    print(out)
    assert "github:NixOS/nixpkgs" in out, "nixpkgs flake not found"
    assert "github:fancy-enterprise/private-flake" in out, "private flake not found"
    cat_log()

    # If no github access token is provided, nix should use the public archive url...
    out = client.succeed("nix flake metadata nixpkgs --json")
    print(out)
    info = json.loads(out)
    assert info["revision"] == "${nixpkgs.rev}", f"revision mismatch: {info['revision']} != ${nixpkgs.rev}"
    cat_log()

    # ... otherwise it should use the API
    out = client.succeed("nix flake metadata private-flake --json --access-tokens github.com=ghp_000000000000000000000000000000000000 --tarball-ttl 0")
    print(out)
    info = json.loads(out)
    assert info["revision"] == "${private-flake-rev}", f"revision mismatch: {info['revision']} != ${private-flake-rev}"
    cat_log()

    client.succeed("nix registry pin nixpkgs")
    client.succeed("nix flake metadata nixpkgs --tarball-ttl 0 >&2")

    # Test fetchTree on a github URL.
    hash = client.succeed(f"nix eval --no-trust-tarballs-from-git-forges --raw --expr '(fetchTree {info['url']}).narHash'")
    assert hash == info['locked']['narHash']

    # Fetching without a narHash should succeed if trust-github is set and fail otherwise.
    client.succeed(f"nix eval --raw --expr 'builtins.fetchTree github:github:fancy-enterprise/private-flake/{info['revision']}'")
    out = client.fail(f"nix eval --no-trust-tarballs-from-git-forges --raw --expr 'builtins.fetchTree github:github:fancy-enterprise/private-flake/{info['revision']}' 2>&1")
    assert "will not fetch unlocked input" in out, "--no-trust-tarballs-from-git-forges did not fail with the expected error"

    # Shut down the web server. The flake should be cached on the client.
    github.succeed("systemctl stop httpd.service")

    info = json.loads(client.succeed("nix flake metadata nixpkgs --json"))
    date = time.strftime("%Y%m%d%H%M%S", time.gmtime(info['lastModified']))
    assert date == "${nixpkgs.lastModifiedDate}", "time mismatch"

    client.succeed("nix build nixpkgs#hello")

    # The build shouldn't fail even with --tarball-ttl 0 (the server
    # being down should not be a fatal error).
    client.succeed("nix build nixpkgs#fuse --tarball-ttl 0")
  '';

}
