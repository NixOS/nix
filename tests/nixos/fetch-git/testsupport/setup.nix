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
        # Setup
        ${indent testCase.setupScript}

        # Test
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
        options.setupScript = mkOption {
          type = types.lines;
          description = ''
            Python code that runs before the test case.
          '';
          default = "";
        };
        options.script = mkOption {
          type = types.lines;
          description = ''
            Python code that runs the test.

            Variables defined by the global `setupScript`, as well as `testCases.*.setupScript` will be available here.
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
    '';
    testScript = ''
      start_all();

      ${config.setupScript}

      ### SETUP COMPLETE ###

      ${lib.concatStringsSep "\n" (map execTestCase config.testCases)}
    '';
  };
}
