{
  description = "can fetch the same repo shallowly and non-shallowly";
  script = ''
    # create branch1 off of main
    client.succeed(f"""
      echo chiang-mai > {repo.path}/thailand \
      && {repo.git} add thailand \
      && {repo.git} commit -m 'commit1' \
      \
      && {repo.git} push origin --all
    """)

    # save the revision
    mainRev = client.succeed(f"""
      {repo.git} rev-parse main
    """).strip()

    # fetch shallowly
    revCountShallow = client.succeed(f"""
      nix eval --impure --expr '
        (builtins.fetchGit {{
          url = "{repo.remote}";
          rev = "{mainRev}";
          shallow = true;
        }}).revCount
      '
    """).strip()
    # ensure the revCount is 0
    assert revCountShallow == "0", f"revCountShallow should be 0, but is {revCountShallow}"

    # fetch non-shallowly
    revCountNonShallow = client.succeed(f"""
      nix eval --impure --expr '
        (builtins.fetchGit {{
          url = "{repo.remote}";
          rev = "{mainRev}";
          shallow = false;
        }}).revCount
      '
    """).strip()
    # ensure the revCount is 1
    assert revCountNonShallow == "1", f"revCountNonShallow should be 1, but is {revCountNonShallow}"

    # fetch shallowly again
    revCountShallow2 = client.succeed(f"""
      nix eval --impure --expr '
        (builtins.fetchGit {{
          url = "{repo.remote}";
          rev = "{mainRev}";
          shallow = true;
        }}).revCount
      '
    """).strip()
    # ensure the revCount is 0
    assert revCountShallow2 == "0", f"revCountShallow2 should be 0, but is {revCountShallow2}"
  '';
}
