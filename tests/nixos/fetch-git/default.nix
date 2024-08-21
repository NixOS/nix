{ lib, config, ... }:
{
  name = "fetch-git";

  imports = [
    ./testsupport/gitea.nix
  ];

  /*
  Test cases

  Test cases are automatically imported from ./test-cases/{name}

  The following is set up automatically for each test case:
    - a repo with the {name} is created on the gitea server
    - a repo with the {name} is created on the client
    - the client repo is configured to push to the server repo

  Python variables:
    - repo.path: the path to the directory of the client repo
    - repo.git: the git command with the client repo as the working directory
    - repo.remote: the url to the server repo
  */
  testCases =
    map
    (testCaseName: {...}: {
      imports = [ (./test-cases + "/${testCaseName}") ];
      # ensures tests are named like their directories they are defined in
      name = testCaseName;
    })
    (lib.attrNames (builtins.readDir ./test-cases));
}
