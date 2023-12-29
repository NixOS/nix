{ lib, config, ... }:
{
  name = "fetch-git";

  imports = [
    ./testsupport/gitea.nix
  ];

  /*
  Test cases
  The following is set up automatically for each test case:
    - a repo with the {name} is created on the gitea server
    - a repo with the {name} is created on the client
    - the client repo is configured to push to the server repo
  Python variables:
    - repo.path: the path to the directory of the client repo
    - repo.git: the git command with the client repo as the working directory
    - repo.remote: the url to the server repo
  */
  testCases = [
    {
      name = "simple-http";
      description = "can fetch a git repo via http";
      script = ''
        # add a file to the repo
        client.succeed(f"""
          echo chiang-mai > {repo.path}/thailand \
          && {repo.git} add thailand \
          && {repo.git} commit -m 'commit1'
        """)

        # memoize the revision
        rev1 = client.succeed(f"""
          {repo.git} rev-parse HEAD
        """).strip()

        # push to the server
        client.succeed(f"""
          {repo.git} push origin main
        """)

        # fetch the repo via nix
        fetched1 = client.succeed(f"""
          nix eval --impure --raw --expr "(builtins.fetchGit {repo.remote}).outPath"
        """)

        # check if the committed file is there
        client.succeed(f"""
          test -f {fetched1}/thailand
        """)

        # check if the revision is the same
        rev1_fetched = client.succeed(f"""
          nix eval --impure --raw --expr "(builtins.fetchGit {repo.remote}).rev"
        """).strip()
        assert rev1 == rev1_fetched
      '';
    }
  ];
}
