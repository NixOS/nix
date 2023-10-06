{
  name = "fetchers-substitute";

  nodes.substituter = { pkgs, ... }: {
    virtualisation.writableStore = true;

    nix.settings.extra-experimental-features = "nix-command";

    networking.firewall.allowedTCPPorts = [ 5000 ];

    services.nix-serve = {
      enable = true;
      secretKeyFile = let key = pkgs.writeTextFile {
        name = "secret-key";
        text = ''
          substituter:SerxxAca5NEsYY0DwVo+subokk+OoHcD9m6JwuctzHgSQVfGHe6nCc+NReDjV3QdFYPMGix4FMg0+K/TM1B3aA==
        '';
      }; in "${key}";
    };
  };

  nodes.importer = { lib, ... }: {
    virtualisation.writableStore = true;

    nix.settings = {
      extra-experimental-features = "nix-command";
      substituters = lib.mkForce [ "http://substituter:5000" ];
      trusted-public-keys = lib.mkForce [ "substituter:EkFXxh3upwnPjUXg41d0HRWDzBoseBTINPiv0zNQd2g=" ];
    };
  };

  testScript = { nodes }: ''
    import json

    start_all()

    missing = "/only-on-substituter.txt"

    substituter.wait_for_unit("multi-user.target")

    substituter.succeed(f"echo 'this should only exist on the substituter' > {missing}")

    hash = substituter.succeed(f"nix hash file {missing}")

    store_path_json = substituter.succeed(f"""
      nix-instantiate --eval --json --read-write-mode --expr '
        builtins.fetchurl {{
          url = "file://{missing}";
          sha256 = "{hash}";
        }}
      '
    """)

    store_path = json.loads(store_path_json)

    substituter.succeed(f"nix store sign --key-file ${nodes.substituter.config.services.nix-serve.secretKeyFile} {store_path}")

    importer.wait_for_unit("multi-user.target")

    importer.succeed(f"""
      nix-instantiate -vvvvv --eval --json --read-write-mode --expr '
        builtins.fetchurl {{
          url = "file://{missing}";
          sha256 = "{hash}";
        }}
      '
    """)
  '';
}
