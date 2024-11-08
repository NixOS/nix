{
  # mostly copied from https://github.com/NixOS/nix/blob/358c26fd13a902d9a4032a00e6683571be07a384/tests/nixos/fetch-git/test-cases/fetchTree-shallow/default.nix#L1
  # ty @DavHau
  description = "fetchGit smudges LFS pointers iff lfs=true";
  script = ''
    # purge nix git cache to make sure we start with a clean slate
    client.succeed("rm -rf ~/.cache/nix")

    # add an lfs-enrolled file to the repo:
    client.succeed(f"dd if=/dev/urandom of={repo.path}/beeg bs=1M count=1")
    client.succeed(f"{repo.git} lfs install")
    client.succeed(f"{repo.git} lfs track --filename \"beeg\"")
    client.succeed(f"{repo.git} add .gitattributes")
    client.succeed(f"{repo.git} add beeg")
    client.succeed(f"{repo.git} commit -m 'commit1'")
    client.succeed(f"{repo.git} push origin main")

    # memoize the revision
    commit1_rev = client.succeed(f"""
      {repo.git} rev-parse HEAD
    """).strip()

    # first fetch without lfs, check that we did not smudge the file
    fetchGit_nolfs_expr = f"""
      builtins.fetchGit {{
        url = "{repo.remote}";
        rev = "{commit1_rev}";
        ref = "main";
        lfs = false;
      }}
    """

    # fetch the repo via nix
    fetched_nolfs = client.succeed(f"""
      nix eval --impure --raw --expr '({fetchGit_nolfs_expr}).outPath'
    """)

    # check that file was not smudged
    file_size_nolfs = client.succeed(f"""
      stat -c %s {fetched_nolfs}/beeg
    """).strip()

    expected_max_size_lfs = 1024
    assert int(file_size_nolfs) < expected_max_size_lfs, f"did not set lfs=true, yet lfs-enrolled file is {file_size_nolfs}b (>{expected_max_size_lfs}b), probably smudged when we should not have"

    # now fetch with lfs=true and check that the file was smudged
    fetchGit_lfs_expr = f"""
      builtins.fetchGit {{
        url = "{repo.remote}";
        rev = "{commit1_rev}";
        ref = "main";
        lfs = true;
      }}
    """

    # fetch the repo via nix
    fetched_lfs = client.succeed(f"""
      nix eval --impure --raw --expr '({fetchGit_lfs_expr}).outPath'
    """)

    # check that file was smudged
    file_size_lfs = client.succeed(f"""
      stat -c %s {fetched_lfs}/beeg
    """).strip()

    expected_min_size_lfs = 1024 * 1024  # 1MB
    assert int(file_size_lfs) >= expected_min_size_lfs, f"set lfs=true, yet lfs-enrolled file is {file_size_lfs}b (<{expected_min_size_lfs}), probably did not smudge when we should have"
  '';
}
