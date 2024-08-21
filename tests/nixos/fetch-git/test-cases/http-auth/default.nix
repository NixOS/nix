{ config, ... }:
{
  description = "can fetch a private git repo via http";
  repo.private = true;
  script = ''
    # add a file to the repo
    client.succeed(f"""
      echo ${config.name /* to make the git tree and store path unique */} > {repo.path}/test-case \
      && echo lutyabrook > {repo.path}/new-york-state \
      && {repo.git} add test-case new-york-state \
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
      test -f {fetched1}/new-york-state
    """)

    # check if the revision is the same
    rev1_fetched = client.succeed(f"""
      nix eval --impure --raw --expr "(builtins.fetchGit {repo.remote}).rev"
    """).strip()
    assert rev1 == rev1_fetched, f"rev1: {rev1} != rev1_fetched: {rev1_fetched}"
  '';
}
