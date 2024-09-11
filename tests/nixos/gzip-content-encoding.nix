# Test that compressed files fetched from server with compressed responses
# do not get excessively decompressed.
# E.g. fetching a zstd compressed tarball from a server,
# which compresses the response with `Content-Encoding: gzip`.
# The expected result is that the fetched file is a zstd archive.

{ lib, config, ... }:

let
  pkgs = config.nodes.machine.nixpkgs.pkgs;

  ztdCompressedFile = pkgs.stdenv.mkDerivation {
    name = "dummy-zstd-compressed-archive";
    dontUnpack = true;
    nativeBuildInputs = with pkgs; [ zstd ];
    buildPhase = ''
      mkdir archive
      for _ in {1..100}; do echo "lorem" > archive/file1; done
      for _ in {1..100}; do echo "ipsum" > archive/file2; done
      tar --zstd -cf archive.tar.zst archive
    '';
    installPhase = ''
      install -Dm 644 -T archive.tar.zst $out/share/archive
    '';
  };

  fileCmd = "${pkgs.file}/bin/file";
in

{
  name = "gzip-content-encoding";

  nodes =
    { machine =
      { config, pkgs, ... }:
      { networking.firewall.allowedTCPPorts = [ 80 ];

        services.nginx.enable = true;
        services.nginx.virtualHosts."localhost" =
          { root = "${ztdCompressedFile}/share/";
            # Make sure that nginx really tries to compress the
            # file on the fly with no regard to size/mime.
            # http://nginx.org/en/docs/http/ngx_http_gzip_module.html
            extraConfig = ''
              gzip on;
              gzip_types *;
              gzip_proxied any;
              gzip_min_length 0;
            '';
          };
        virtualisation.writableStore = true;
        virtualisation.additionalPaths = with pkgs; [ file ];
        nix.settings.substituters = lib.mkForce [ ];
      };
    };

  # Check that when nix-prefetch-url is used with a zst tarball it does not get decompressed.
  testScript = { nodes }: ''
    # fmt: off
    start_all()

    machine.wait_for_unit("nginx.service")
    machine.succeed("""
      # Make sure that the file is properly compressed as the test would be meaningless otherwise
      curl --compressed -v http://localhost/archive |& tr -s ' ' |& grep --ignore-case 'content-encoding: gzip'
      archive_path=$(nix-prefetch-url http://localhost/archive --print-path | tail -n1)
      [[ $(${fileCmd} --brief --mime-type $archive_path) == "application/zstd" ]]
      tar --zstd -xf $archive_path
    """)
  '';
}
