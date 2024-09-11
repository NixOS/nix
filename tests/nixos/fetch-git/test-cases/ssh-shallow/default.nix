{
  description = "can fetch a git repo via ssh using shallow=1";
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
      {repo.git} push origin-ssh main
    """)

    fetchGit_expr = f"""
      builtins.fetchGit {{
        url = "{repo.remote_ssh}";
        rev = "{rev1}";
        shallow = true;
      }}
    """

    # fetch the repo via nix
    fetched1 = client.succeed(f"""
      nix eval --impure --raw --expr '({fetchGit_expr}).outPath'
    """)

    # check if the committed file is there
    client.succeed(f"""
      test -f {fetched1}/thailand
    """)

    # check if the revision is the same
    rev1_fetched = client.succeed(f"""
      nix eval --impure --raw --expr '({fetchGit_expr}).rev'
    """).strip()
    assert rev1 == rev1_fetched, f"rev1: {rev1} != rev1_fetched: {rev1_fetched}"

    # check if revCount is 1
    revCount1 = client.succeed(f"""
      nix eval --impure --expr '({fetchGit_expr}).revCount'
    """).strip()
    print(f"revCount1: {revCount1}")
    assert revCount1 == '0', f"rev count is not 0 but {revCount1}"
  '';
}
