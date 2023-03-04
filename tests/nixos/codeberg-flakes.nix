{
  lib,
  config,
  nixpkgs,
  ...
}: let
  inherit (config.nodes.client.nixpkgs) pkgs;

  # Generate a fake root CA and a fake codeberg.org / channels.nixos.org certificate.
  cert =
    pkgs.runCommand "cert" {nativeBuildInputs = [pkgs.openssl];}
    ''
      mkdir -p $out

      openssl genrsa -out ca.key 2048
      openssl req -new -x509 -days 36500 -key ca.key \
        -subj "/C=NL/ST=Denial/L=Springfield/O=Dis/CN=Root CA" -out $out/ca.crt

      openssl req -newkey rsa:2048 -nodes -keyout $out/server.key \
        -subj "/C=CN/ST=Denial/L=Springfield/O=Dis/CN=codeberg.org" -out server.csr
      openssl x509 -req -extfile <(printf "subjectAltName=DNS:codeberg.org,DNS:channels.nixos.org") \
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
              "type": "codeberg",
              "owner": "NixOS",
              "repo": "nixpkgs",
              "ref": "main"
            }
          },
          {
            "from": {
              "type": "indirect",
              "id": "private-flake"
            },
            "to": {
              "type": "codeberg",
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

  private-flake-api =
    pkgs.runCommand "private-flake" {}
    ''
      mkdir -p $out/{archive,branches}

      echo '{"default_branch": "main"}' > $out/index.json

      # Setup https://codeberg.org/api/swagger#/repository/repoListBranches
      # This is simulating the use of the default HEAD ref
      echo '{"commit": {"id": "${private-flake-rev}"}}' > $out/branches/main

      # Setup tarball download via API
      dir=private-flake-${private-flake-rev}
      mkdir $dir
      echo '{ outputs = {...}: {}; }' > $dir/flake.nix
      tar cfz $out/archive/${private-flake-rev}.tar.gz $dir --hard-dereference
    '';

  nixpkgs-api =
    pkgs.runCommand "nixpkgs-flake" {}
    ''
      mkdir -p $out/branches

      # Setup https://codeberg.org/api/swagger#/repository/repoListGitRefs
      # This is simulating the use of a specific branch
      echo '{"commit": {"id": "${nixpkgs.rev}"}}' > $out/branches/main
    '';

  archive =
    pkgs.runCommand "nixpkgs-flake" {}
    ''
      mkdir -p $out/archive

      dir=NixOS-nixpkgs-${nixpkgs.shortRev}
      cp -prd ${nixpkgs} $dir
      # Set the correct timestamp in the tarball.
      find $dir -print0 | xargs -0 touch -t ${builtins.substring 0 12 nixpkgs.lastModifiedDate}.${builtins.substring 12 2 nixpkgs.lastModifiedDate} --
      tar cfz $out/archive/${nixpkgs.rev}.tar.gz $dir --hard-dereference
    '';
in {
  name = "codeberg-flakes";

  nodes = {
    codeberg = {
      config,
      pkgs,
      ...
    }: {
      networking.firewall.allowedTCPPorts = [80 443];

      services.httpd.enable = true;
      services.httpd.adminAddr = "foo@example.org";
      services.httpd.extraConfig = ''
        ErrorLog syslog:local6
      '';
      services.httpd.virtualHosts."channels.nixos.org" = {
        forceSSL = true;
        sslServerKey = "${cert}/server.key";
        sslServerCert = "${cert}/server.crt";
        servedDirs = [
          {
            urlPath = "/";
            dir = registry;
          }
        ];
      };
      services.httpd.virtualHosts."codeberg.org" = {
        forceSSL = true;
        sslServerKey = "${cert}/server.key";
        sslServerCert = "${cert}/server.crt";
        servedDirs = [
          {
            urlPath = "/api/v1/repos/fancy-enterprise/private-flake";
            dir = private-flake-api;
          }
          {
            urlPath = "/api/v1/repos/NixOS/nixpkgs";
            dir = nixpkgs-api;
          }
          {
            urlPath = "/NixOS/nixpkgs";
            dir = archive;
          }
        ];
        # NOTE: while this works well to emulate this endpoint, it technically isn't identical,
        # as httpd will redirect and add a trailing / first.
        locations."/api/v1/repos/fancy-enterprise/private-flake".index = "index.json";
      };
    };

    client = {
      config,
      lib,
      pkgs,
      nodes,
      ...
    }: {
      virtualisation.writableStore = true;
      virtualisation.diskSize = 2048;
      virtualisation.additionalPaths = [pkgs.hello pkgs.fuse];
      virtualisation.memorySize = 4096;
      nix.settings.substituters = lib.mkForce [];
      nix.extraOptions = "experimental-features = nix-command flakes";
      networking.hosts.${(builtins.head nodes.codeberg.config.networking.interfaces.eth1.ipv4.addresses).address} = ["channels.nixos.org" "codeberg.org"];
      security.pki.certificateFiles = ["${cert}/ca.crt"];
    };
  };

  testScript = {nodes}: ''
    # fmt: off
    import json
    import time

    start_all()

    def cat_log():
         codeberg.succeed("cat /var/log/httpd/*.log >&2")

    codeberg.wait_for_unit("httpd.service")

    client.succeed("curl -v https://codeberg.org/ >&2")
    out = client.succeed("nix registry list")
    print(out)
    assert "codeberg:NixOS/nixpkgs" in out, "nixpkgs flake not found"
    assert "codeberg:fancy-enterprise/private-flake" in out, "private flake not found"
    cat_log()

    # If no codeberg access token is provided, nix should use the public archive url...
    out = client.succeed("nix flake metadata nixpkgs --json")
    print(out)
    info = json.loads(out)
    assert info["revision"] == "${nixpkgs.rev}", f"revision mismatch: {info['revision']} != ${nixpkgs.rev}"
    cat_log()

    # ... otherwise it should use the API
    out = client.succeed("nix flake metadata private-flake --json --access-tokens codeberg.org=65eaa9c8ef52460d22a93307fe0aee76289dc675 --tarball-ttl 0")
    print(out)
    info = json.loads(out)
    assert info["revision"] == "${private-flake-rev}", f"revision mismatch: {info['revision']} != ${private-flake-rev}"
    cat_log()

    client.succeed("nix registry pin nixpkgs")
    client.succeed("nix flake metadata nixpkgs --tarball-ttl 0 >&2")

    # Shut down the web server. The flake should be cached on the client.
    codeberg.succeed("systemctl stop httpd.service")

    info = json.loads(client.succeed("nix flake metadata nixpkgs --json"))
    date = time.strftime("%Y%m%d%H%M%S", time.gmtime(info['lastModified']))
    assert date == "${nixpkgs.lastModifiedDate}", "time mismatch"

    client.succeed("nix build nixpkgs#hello")

    # The build shouldn't fail even with --tarball-ttl 0 (the server
    # being down should not be a fatal error).
    client.succeed("nix build nixpkgs#fuse --tarball-ttl 0")
  '';
}
