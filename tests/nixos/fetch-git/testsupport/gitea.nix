{
  lib,
  nixpkgs,
  system,
  pkgs,
  ...
}:
let
  clientPrivateKey = pkgs.writeText "id_ed25519" ''
    -----BEGIN OPENSSH PRIVATE KEY-----
    b3BlbnNzaC1rZXktdjEAAAAABG5vbmUAAAAEbm9uZQAAAAAAAAABAAAAMwAAAAtzc2gtZW
    QyNTUxOQAAACBbeWvHh/AWGWI6EIc1xlSihyXtacNQ9KeztlW/VUy8wQAAAJAwVQ5VMFUO
    VQAAAAtzc2gtZWQyNTUxOQAAACBbeWvHh/AWGWI6EIc1xlSihyXtacNQ9KeztlW/VUy8wQ
    AAAEB7lbfkkdkJoE+4TKHPdPQWBKLSx+J54Eg8DaTr+3KoSlt5a8eH8BYZYjoQhzXGVKKH
    Je1pw1D0p7O2Vb9VTLzBAAAACGJmb0BtaW5pAQIDBAU=
    -----END OPENSSH PRIVATE KEY-----
  '';

  clientPublicKey = "ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIFt5a8eH8BYZYjoQhzXGVKKHJe1pw1D0p7O2Vb9VTLzB";

in
{
  imports = [
    ../testsupport/setup.nix
    ../testsupport/gitea-repo.nix
  ];
  nodes = {
    gitea =
      { pkgs, ... }:
      {
        services.gitea.enable = true;
        services.gitea.lfs.enable = true;
        services.gitea.settings = {
          service.DISABLE_REGISTRATION = true;
          server = {
            DOMAIN = "gitea";
            HTTP_PORT = 3000;
            SSH_PORT = 3001;
            START_SSH_SERVER = true;
          };
          log.LEVEL = "Info";
          database.LOG_SQL = false;
        };
        networking.firewall.allowedTCPPorts = [
          3000
          3001
        ];
        environment.systemPackages = [
          pkgs.git
          pkgs.gitea
        ];
      };
    client =
      { pkgs, ... }:
      {
        environment.systemPackages = [
          pkgs.git
          pkgs.git-lfs
        ];
      };
  };

  setupScript = ''
    import shlex

    gitea.wait_for_unit("gitea.service")

    gitea_user = "test"
    gitea_password = "test123test"

    gitea.succeed(f"""
      gitea --version >&2
      su -l gitea -c 'GITEA_WORK_DIR=/var/lib/gitea gitea admin user create \
        --username {gitea_user} --password {gitea_password} --email test@client'
    """)

    client.wait_for_unit("multi-user.target")
    gitea.wait_for_open_port(3000)
    gitea.wait_for_open_port(3001)

    gitea.succeed(f"""
      curl --fail -X POST http://{gitea_user}:{gitea_password}@gitea:3000/api/v1/user/keys \
        -H 'Accept: application/json' -H 'Content-Type: application/json' \
        -d {shlex.quote( '{"title":"key", "key":"${clientPublicKey}", "read_only": false}' )} >&2
    """)

    client.succeed(f"""
      echo "http://{gitea_user}:{gitea_password}@gitea:3000" >~/.git-credentials-admin
      git config --global credential.helper 'store --file ~/.git-credentials-admin'
      git config --global user.email "test@client"
      git config --global user.name "Test User"
      git config --global gc.autodetach 0
      git config --global gc.auto 0
    """)

    # add client's private key to ~/.ssh
    client.succeed("""
      mkdir -p ~/.ssh
      chmod 700 ~/.ssh
      cat ${clientPrivateKey} >~/.ssh/id_ed25519
      chmod 600 ~/.ssh/id_ed25519
    """)

    client.succeed("""
      echo "Host gitea" >>~/.ssh/config
      echo "  StrictHostKeyChecking no" >>~/.ssh/config
      echo "  UserKnownHostsFile /dev/null" >>~/.ssh/config
      echo "  User gitea" >>~/.ssh/config
    """)
  '';
}
