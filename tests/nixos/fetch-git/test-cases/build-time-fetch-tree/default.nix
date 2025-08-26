{ config, ... }:
{
  description = "build-time fetching";
  script = ''
    import json

    # add a file to the repo
    client.succeed(f"""
      echo ${config.name # to make the git tree and store path unique
      } > {repo.path}/test-case \
      && echo chiang-mai > {repo.path}/thailand \
      && {repo.git} add test-case thailand \
      && {repo.git} commit -m 'commit1' \
      && {repo.git} push origin main
    """)

    # get the NAR hash
    nar_hash = json.loads(client.succeed(f"""
      nix flake prefetch --flake-registry "" git+{repo.remote} --json
    """))['hash']

    # construct the derivation
    expr = f"""
      derivation {{
        name = "source";
        builder = "builtin:fetch-tree";
        system = "builtin";
        __structuredAttrs = true;
        input = {{
          type = "git";
          url = "{repo.remote}";
          ref = "main";
        }};
        outputHashMode = "recursive";
        outputHash = "{nar_hash}";
      }}
    """

    # do the build-time fetch
    out_path = client.succeed(f"""
      nix build --print-out-paths --store /run/store --flake-registry "" --extra-experimental-features build-time-fetch-tree --expr '{expr}'
    """).strip()

    # check if the committed file is there
    client.succeed(f"""
      test -f /run/store/{out_path}/thailand
    """)
  '';
}
