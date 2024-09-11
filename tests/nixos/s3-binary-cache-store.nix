{ lib, config, nixpkgs, ... }:

let
  pkgs = config.nodes.client.nixpkgs.pkgs;

  pkgA = pkgs.cowsay;

  accessKey = "BKIKJAA5BMMU2RHO6IBB";
  secretKey = "V7f1CwQqAcwo80UEIJEjc5gVQUSSx5ohQ9GSrr12";
  env = "AWS_ACCESS_KEY_ID=${accessKey} AWS_SECRET_ACCESS_KEY=${secretKey}";

  storeUrl = "s3://my-cache?endpoint=http://server:9000&region=eu-west-1";

in {
  name = "nix-copy-closure";

  nodes =
    { server =
        { config, lib, pkgs, ... }:
        { virtualisation.writableStore = true;
          virtualisation.additionalPaths = [ pkgA ];
          environment.systemPackages = [ pkgs.minio-client ];
          nix.extraOptions = "experimental-features = nix-command";
          services.minio = {
            enable = true;
            region = "eu-west-1";
            rootCredentialsFile = pkgs.writeText "minio-credentials-full" ''
              MINIO_ROOT_USER=${accessKey}
              MINIO_ROOT_PASSWORD=${secretKey}
            '';
          };
          networking.firewall.allowedTCPPorts = [ 9000 ];
        };

      client =
        { config, pkgs, ... }:
        { virtualisation.writableStore = true;
          nix.extraOptions = "experimental-features = nix-command";
        };
    };

  testScript = { nodes }: ''
    # fmt: off
    start_all()

    # Create a binary cache.
    server.wait_for_unit("minio")

    server.succeed("mc config host add minio http://localhost:9000 ${accessKey} ${secretKey} --api s3v4")
    server.succeed("mc mb minio/my-cache")

    server.succeed("${env} nix copy --to '${storeUrl}' ${pkgA}")

    # Copy a package from the binary cache.
    client.fail("nix path-info ${pkgA}")

    client.succeed("${env} nix store info --store '${storeUrl}' >&2")

    client.succeed("${env} nix copy --no-check-sigs --from '${storeUrl}' ${pkgA}")

    client.succeed("nix path-info ${pkgA}")
  '';
}
