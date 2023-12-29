{ lib, nixpkgs, system, ... }: {
  imports = [
    ../testsupport/setup.nix
  ];
  nodes = {
    gitea = { pkgs, ... }: {
      services.gitea.enable = true;
      services.gitea.settings.service.DISABLE_REGISTRATION = true;
      services.gitea.settings.log.LEVEL = "Info";
      services.gitea.settings.database.LOG_SQL = false;
      networking.firewall.allowedTCPPorts = [ 3000 ];
      environment.systemPackages = [ pkgs.gitea ];

      # TODO: remove this after updating to nixos-23.11
      nixpkgs.pkgs = lib.mkForce (import nixpkgs {
        inherit system;
        config.permittedInsecurePackages = [
          "gitea-1.19.4"
        ];
      });
    };
    client = { pkgs, ... }: {
      environment.systemPackages = [ pkgs.git ];
    };
  };
  defaults = { pkgs, ... }: {
    environment.systemPackages = [ pkgs.jq ];
  };

  setupScript = ''
    import shlex

    gitea.wait_for_unit("gitea.service")

    gitea_admin = "test"
    gitea_admin_password = "test123test"

    gitea.succeed(f"""
      gitea --version >&2
      su -l gitea -c 'GITEA_WORK_DIR=/var/lib/gitea gitea admin user create \
        --username {gitea_admin} --password {gitea_admin_password} --email test@client'
    """)

    client.wait_for_unit("multi-user.target")
    gitea.wait_for_open_port(3000)

    gitea_admin_token = gitea.succeed(f"""
      curl --fail -X POST http://{gitea_admin}:{gitea_admin_password}@gitea:3000/api/v1/users/test/tokens \
        -H 'Accept: application/json' -H 'Content-Type: application/json' \
        -d {shlex.quote( '{"name":"token", "scopes":["all"]}' )} \
        | jq -r '.sha1'
    """).strip()

    client.succeed(f"""
      echo "http://{gitea_admin}:{gitea_admin_password}@gitea:3000" >~/.git-credentials-admin
      git config --global credential.helper 'store --file ~/.git-credentials-admin'
      git config --global user.email "test@client"
      git config --global user.name "Test User"
      git config --global gc.autodetach 0
      git config --global gc.auto 0
    """)
  '';
}
