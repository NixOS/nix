{ lib, ... }:
let
  inherit (lib) mkOption types;

  testCaseExtension = { config, ... }: {
    setupScript = ''
      repo = Repo("${config.name}")
    '';
  };
in
{
  options = {
    testCases = mkOption {
      type = types.listOf (types.submodule testCaseExtension);
    };
  };
  config = {
    setupScript = ''
      class Repo:
        """
        A class to create a git repository on the gitea server and locally.
        """
        def __init__(self, name):
          self.name = name
          self.path = "/tmp/repos/" + name
          self.remote = "http://gitea:3000/test/" + name
          self.remote_ssh = "ssh://gitea/root/" + name
          self.git = f"git -C {self.path}"
          self.create()

        def create(self):
          # create ssh remote repo
          gitea.succeed(f"""
            git init --bare -b main /root/{self.name}
          """)
          # create http remote repo
          gitea.succeed(f"""
            curl --fail -X POST http://{gitea_admin}:{gitea_admin_password}@gitea:3000/api/v1/user/repos \
              -H 'Accept: application/json' -H 'Content-Type: application/json' \
              -d {shlex.quote( f'{{"name":"{self.name}", "default_branch": "main"}}' )}
          """)
          # setup git remotes on client
          client.succeed(f"""
            mkdir -p {self.path} \
            && git init -b main {self.path} \
            && {self.git} remote add origin {self.remote} \
            && {self.git} remote add origin-ssh root@gitea:{self.name}
          """)
    '';
  };
}