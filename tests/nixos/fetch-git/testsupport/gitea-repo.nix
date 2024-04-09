{ lib, ... }:
let
  inherit (lib)
    mkIf
    mkOption
    types
    ;

  boolPyLiteral = b: if b then "True" else "False";

  testCaseExtension = { config, ... }: {
    options = {
      repo.enable = mkOption {
        type = types.bool;
        default = true;
        description = "Whether to provide a repo variable - automatic repo creation.";
      };
      repo.private = mkOption {
        type = types.bool;
        default = false;
        description = "Whether the repo should be private.";
      };
    };
    config = mkIf config.repo.enable {
      setupScript = ''
        repo = Repo("${config.name}", private=${boolPyLiteral config.repo.private})
      '';
    };
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
      def boolToJSON(b):
        return "true" if b else "false"

      class Repo:
        """
        A class to create a git repository on the gitea server and locally.
        """
        def __init__(self, name, private=False):
          self.name = name
          self.path = "/tmp/repos/" + name
          self.remote = "http://gitea:3000/test/" + name
          self.remote_ssh = "ssh://gitea/root/" + name
          self.git = f"git -C {self.path}"
          self.private = private
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
              -d {shlex.quote( f'{{"name":"{self.name}", "default_branch": "main", "private": {boolToJSON(self.private)}}}' )}
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
