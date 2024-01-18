{ lib, config, extendModules, ... }:
let
  inherit (lib)
    mkOption
    types
    ;

  indent = lib.replaceStrings ["\n"] ["\n    "];

  execTestCase = testCase: ''

    ### TEST ${testCase.name}: ${testCase.description} ###

    with subtest("${testCase.description}"):
        repo = Repo("${testCase.name}")
        ${indent testCase.script}
  '';
in
{

  options = {
    setupScript = mkOption {
      type = types.lines;
      description = ''
        Python code that runs before the main test.

        Variables defined by this code will be available in the test.
      '';
      default = "";
    };
    testCases = mkOption {
      description = ''
        The test cases. See `testScript`.
      '';
      type = types.listOf (types.submodule {
        options.name = mkOption {
          type = types.str;
          description = ''
            The name of the test case.

            A repository with that name will be set up on the gitea server and locally.
          '';
        };
        options.description = mkOption {
          type = types.str;
          description = ''
            A description of the test case.
          '';
        };
        options.script = mkOption {
          type = types.lines;
          description = ''
            Python code that runs the test.

            Variables defined by `setupScript` will be available here.
          '';
        };
      });
    };
  };

  config = {
    nodes.client = {
      environment.variables = {
        _NIX_FORCE_HTTP = "1";
      };
      nix.settings.experimental-features = ["nix-command" "flakes"];
    };
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
    testScript = ''
      start_all();

      ${config.setupScript}

      ### SETUP COMPLETE ###

      ${lib.concatStringsSep "\n" (map execTestCase config.testCases)}
    '';
  };
}
