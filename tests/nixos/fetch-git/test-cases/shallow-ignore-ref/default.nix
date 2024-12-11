{
  description = "ensure that ref gets ignored when shallow=true is set";
  script = ''
    # create branch1 off of main
    client.succeed(f"""
      echo chiang-mai > {repo.path}/thailand \
      && {repo.git} add thailand \
      && {repo.git} commit -m 'commit1' \
      \
      && {repo.git} checkout -b branch1 main \
      && echo bangkok > {repo.path}/thailand \
      && {repo.git} add thailand \
      && {repo.git} commit -m 'commit2' \
      \
      && {repo.git} push origin --all
    """)

    # save the revisions
    mainRev = client.succeed(f"""
      {repo.git} rev-parse main
    """).strip()
    branch1Rev = client.succeed(f"""
      {repo.git} rev-parse branch1
    """).strip()

    # Ensure that ref gets ignored when fetching shallowly.
    # This would fail if the ref was respected, as branch1Rev is not on main.
    client.succeed(f"""
      nix eval --impure --raw --expr '
        (builtins.fetchGit {{
          url = "{repo.remote}";
          rev = "{branch1Rev}";
          ref = "main";
          shallow = true;
        }})
      '
    """)

  '';
}
