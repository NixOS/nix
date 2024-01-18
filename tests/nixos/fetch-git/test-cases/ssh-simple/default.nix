{ config, ... }:
{
  description = "can fetch a git repo via ssh";
  script = ''
    # add a file to the repo
    client.succeed(f"""
      echo ${config.name /* to make the git tree and store path unique */} > {repo.path}/test-case \
      && echo chiang-mai > {repo.path}/thailand \
      && {repo.git} add test-case thailand \
      && {repo.git} commit -m 'commit1'
    """)

    # save the revision
    rev1 = client.succeed(f"""
      {repo.git} rev-parse HEAD
    """).strip()

    # push to the server
    client.succeed(f"""
      {repo.git} push origin-ssh main
    """)

    # fetch the repo via nix
    fetched1 = client.succeed(f"""
      nix eval --impure --raw --expr '
        (builtins.fetchGit "{repo.remote_ssh}").outPath
      '
    """)

    # check if the committed file is there
    client.succeed(f"""
      test -f {fetched1}/thailand
    """)

    # check if the revision is the same
    rev1_fetched = client.succeed(f"""
      nix eval --impure --raw --expr '
        (builtins.fetchGit "{repo.remote_ssh}").rev
      '
    """).strip()
    assert rev1 == rev1_fetched, f"rev1: {rev1} != rev1_fetched: {rev1_fetched}"
  '';
}
