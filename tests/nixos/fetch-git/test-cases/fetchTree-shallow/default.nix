{
  description = "fetchTree fetches git repos shallowly by default";
  script = ''
    # purge nix git cache to make sure we start with a clean slate
    client.succeed("rm -rf ~/.cache/nix")

    # add two commits to the repo:
    #   - one with a large file (2M)
    #   - another one making the file small again
    client.succeed(f"""
      dd if=/dev/urandom of={repo.path}/thailand bs=1M count=2 \
      && {repo.git} add thailand \
      && {repo.git} commit -m 'commit1' \
      && echo 'ThaigerSprint' > {repo.path}/thailand \
      && {repo.git} add thailand \
      && {repo.git} commit -m 'commit2' \
      && {repo.git} push origin main
    """)

    # memoize the revision
    commit2_rev = client.succeed(f"""
      {repo.git} rev-parse HEAD
    """).strip()

    # construct the fetcher call
    fetchGit_expr = f"""
      builtins.fetchTree {{
        type = "git";
        url = "{repo.remote}";
        rev = "{commit2_rev}";
      }}
    """

    # fetch the repo via nix
    fetched1 = client.succeed(f"""
      nix eval --impure --raw --expr '({fetchGit_expr}).outPath'
    """)

    # check that the size of ~/.cache/nix is less than 1M
    cache_size = client.succeed("""
      du -s ~/.cache/nix
    """).strip().split()[0]
    assert int(cache_size) < 1024, f"cache size is {cache_size}K which is larger than 1M"
  '';
}
