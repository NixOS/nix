{
  # mostly copied from https://github.com/NixOS/nix/blob/358c26fd13a902d9a4032a00e6683571be07a384/tests/nixos/fetch-git/test-cases/fetchTree-shallow/default.nix#L1
  # ty @DavHau
  description = "fetchGit smudges LFS pointers if lfs=true";
  script = ''
    from tempfile import TemporaryDirectory

    expected_max_size_lfs_pointer = 1024 # 1 KiB (values >= than this cannot be pointers, and test files are 1 MiB)

    # purge nix git cache to make sure we start with a clean slate
    client.succeed("rm -rf ~/.cache/nix")


    with subtest("Request lfs fetch without any .gitattributes file"):
      client.succeed(f"dd if=/dev/urandom of={repo.path}/regular bs=1M count=1 >&2")
      client.succeed(f"{repo.git} add : >&2")
      client.succeed(f"{repo.git} commit -m 'no .gitattributes' >&2")
      client.succeed(f"{repo.git} push origin main >&2")

      # memorize the revision
      no_gitattributes_rev = client.succeed(f"{repo.git} rev-parse HEAD").strip()

      # fetch with lfs=true, and check that the lack of .gitattributes does not break anything
      fetchGit_no_gitattributes_expr = f"""
        builtins.fetchGit {{
          url = "{repo.remote}";
          rev = "{no_gitattributes_rev}";
          ref = "main";
          lfs = true;
        }}
      """
      fetched_no_gitattributes = client.succeed(f"""
        nix eval --debug --impure --raw --expr '({fetchGit_no_gitattributes_expr}).outPath'
      """)
      client.succeed(f"cmp {repo.path}/regular {fetched_no_gitattributes}/regular >&2")


    with subtest("Add a file that should be tracked by lfs, but isn't"):
      # (git lfs cli only throws a warning "Encountered 1 file that should have
      # been a pointer, but wasn't")

      client.succeed(f"dd if=/dev/urandom of={repo.path}/black_sheep bs=1M count=1 >&2")
      client.succeed(f"echo 'black_sheep filter=lfs -text' >>{repo.path}/.gitattributes")
      client.succeed(f"{repo.git} add : >&2")
      client.succeed(f"{repo.git} commit -m 'add misleading file' >&2")
      client.succeed(f"{repo.git} push origin main >&2")

      # memorize the revision
      bad_lfs_rev = client.succeed(f"{repo.git} rev-parse HEAD").strip()

      # test assumption that it can be cloned with regular git first
      # (here we see the warning as stated above)
      with TemporaryDirectory() as tempdir:
        client.succeed(f"git clone -n {repo.remote} {tempdir} >&2")
        client.succeed(f"git -C {tempdir} lfs install >&2")
        client.succeed(f"git -C {tempdir} checkout {bad_lfs_rev} >&2")

        # check that the file is not a pointer, as expected
        file_size_git = client.succeed(f"stat -c %s {tempdir}/black_sheep").strip()
        assert int(file_size_git) == 1024 * 1024, \
          f"non lfs file is {file_size_git}b (!= 1MiB), probably a test implementation error"

        lfs_files = client.succeed(f"git -C {tempdir} lfs ls-files").strip()
        assert lfs_files == "", "non lfs file is tracked by lfs, probably a test implementation error"

        client.succeed(f"cmp {repo.path}/black_sheep {tempdir}/black_sheep >&2")

      # now fetch without lfs, check that the file is not a pointer
      fetchGit_bad_lfs_without_lfs_expr = f"""
        builtins.fetchGit {{
          url = "{repo.remote}";
          rev = "{bad_lfs_rev}";
          ref = "main";
          lfs = false;
        }}
      """
      fetched_bad_lfs_without_lfs = client.succeed(f"""
        nix eval --debug --impure --raw --expr '({fetchGit_bad_lfs_without_lfs_expr}).outPath'
      """)

      # check that file was not somehow turned into a pointer
      file_size_bad_lfs_without_lfs = client.succeed(f"stat -c %s {fetched_bad_lfs_without_lfs}/black_sheep").strip()

      assert int(file_size_bad_lfs_without_lfs) == 1024 * 1024, \
        f"non lfs-enrolled file is {file_size_bad_lfs_without_lfs}b (!= 1MiB), probably a test implementation error"
      client.succeed(f"cmp {repo.path}/black_sheep {fetched_bad_lfs_without_lfs}/black_sheep >&2")

      # finally fetch with lfs=true, and check that the bad file does not break anything
      fetchGit_bad_lfs_with_lfs_expr = f"""
        builtins.fetchGit {{
          url = "{repo.remote}";
          rev = "{bad_lfs_rev}";
          ref = "main";
          lfs = true;
        }}
      """
      fetchGit_bad_lfs_with_lfs = client.succeed(f"""
        nix eval --debug --impure --raw --expr '({fetchGit_bad_lfs_with_lfs_expr}).outPath'
      """)

      client.succeed(f"cmp {repo.path}/black_sheep {fetchGit_bad_lfs_with_lfs}/black_sheep >&2")


    with subtest("Add an lfs-enrolled file to the repo"):
      client.succeed(f"dd if=/dev/urandom of={repo.path}/beeg bs=1M count=1 >&2")
      client.succeed(f"{repo.git} lfs install >&2")
      client.succeed(f"{repo.git} lfs track --filename beeg >&2")
      client.succeed(f"{repo.git} add : >&2")
      client.succeed(f"{repo.git} commit -m 'add lfs file' >&2")
      client.succeed(f"{repo.git} push origin main >&2")

      # memorize the revision
      lfs_file_rev = client.succeed(f"{repo.git} rev-parse HEAD").strip()

      # first fetch without lfs, check that we did not smudge the file
      fetchGit_nolfs_expr = f"""
        builtins.fetchGit {{
          url = "{repo.remote}";
          rev = "{lfs_file_rev}";
          ref = "main";
          lfs = false;
        }}
      """
      fetched_nolfs = client.succeed(f"""
        nix eval --debug --impure --raw --expr '({fetchGit_nolfs_expr}).outPath'
      """)

      # check that file was not smudged
      file_size_nolfs = client.succeed(f"stat -c %s {fetched_nolfs}/beeg").strip()

      assert int(file_size_nolfs) < expected_max_size_lfs_pointer, \
        f"did not set lfs=true, yet lfs-enrolled file is {file_size_nolfs}b (>= 1KiB), probably smudged when we should not have"

      # now fetch with lfs=true and check that the file was smudged
      fetchGit_lfs_expr = f"""
        builtins.fetchGit {{
          url = "{repo.remote}";
          rev = "{lfs_file_rev}";
          ref = "main";
          lfs = true;
        }}
      """
      fetched_lfs = client.succeed(f"""
        nix eval --debug --impure --raw --expr '({fetchGit_lfs_expr}).outPath'
      """)

      assert fetched_lfs != fetched_nolfs, \
        f"fetching with and without lfs yielded the same store path {fetched_lfs}, fingerprinting error?"

      # check that file was smudged
      file_size_lfs = client.succeed(f"stat -c %s {fetched_lfs}/beeg").strip()
      assert int(file_size_lfs) == 1024 * 1024, \
        f"set lfs=true, yet lfs-enrolled file is {file_size_lfs}b (!= 1MiB), probably did not smudge when we should have"


    with subtest("Check that default is lfs=false"):
      fetchGit_default_expr = f"""
        builtins.fetchGit {{
          url = "{repo.remote}";
          rev = "{lfs_file_rev}";
          ref = "main";
        }}
      """
      fetched_default = client.succeed(f"""
        nix eval --debug --impure --raw --expr '({fetchGit_default_expr}).outPath'
      """)

      # check that file was not smudged
      file_size_default = client.succeed(f"stat -c %s {fetched_default}/beeg").strip()

      assert int(file_size_default) < expected_max_size_lfs_pointer, \
        f"did not set lfs, yet lfs-enrolled file is {file_size_default}b (>= 1KiB), probably bad default value"

    with subtest("Use as flake input"):
      # May seem reduntant, but this has minor differences compared to raw
      # fetchGit which caused failures before
      with TemporaryDirectory() as tempdir:
        client.succeed(f"mkdir -p {tempdir}")
        client.succeed(f"""
          printf '{{
            inputs = {{
              foo = {{
                url = "git+{repo.remote}?ref=main&rev={lfs_file_rev}&lfs=1";
                flake = false;
              }};
            }};
            outputs = {{ foo, self }}: {{ inherit (foo) outPath; }};
          }}' >{tempdir}/flake.nix
        """)
        fetched_flake = client.succeed(f"""
          nix eval --debug --raw {tempdir}#.outPath
        """)

      assert fetched_lfs == fetched_flake, \
        f"fetching as flake input (store path {fetched_flake}) yielded a different result than using fetchGit (store path {fetched_lfs})"


    with subtest("Check self.lfs"):
      client.succeed(f"""
        printf '{{
          inputs.self.lfs = true;
          outputs = {{ self }}: {{ }};
        }}' >{repo.path}/flake.nix
      """)
      client.succeed(f"{repo.git} add : >&2")
      client.succeed(f"{repo.git} commit -m 'add flake' >&2")
      client.succeed(f"{repo.git} push origin main >&2")

      # memorize the revision
      self_lfs_rev = client.succeed(f"{repo.git} rev-parse HEAD").strip()

      # Check that LFS is now enabled by default
      with TemporaryDirectory() as tempdir:
        client.succeed(f"mkdir -p {tempdir}")
        client.succeed(f"""
          printf '{{
            inputs.foo = {{
              url = "git+{repo.remote}?ref=main&rev={self_lfs_rev}";
            }};
            outputs = {{ foo, self }}: {{ inherit (foo) outPath; }};
          }}' >{tempdir}/flake.nix
        """)
        fetched_self_lfs = client.succeed(f"""
          nix eval --debug --raw {tempdir}#.outPath
        """)

      client.succeed(f"cmp {repo.path}/beeg {fetched_self_lfs}/beeg >&2")

      # Check that we can disable LFS enabled by default
      with TemporaryDirectory() as tempdir:
        client.succeed(f"mkdir -p {tempdir}")
        client.succeed(f"""
          printf '{{
            inputs.foo = {{
              url = "git+{repo.remote}?ref=main&rev={self_lfs_rev}&lfs=0";
            }};
            outputs = {{ foo, self }}: {{ inherit (foo) outPath; }};
          }}' >{tempdir}/flake.nix
        """)
        fetched_self_nolfs = client.succeed(f"""
          nix eval --debug --raw {tempdir}#.outPath
        """)

      client.succeed(f"! cmp {fetched_self_lfs}/beeg {fetched_self_nolfs}/beeg >&2")


    with subtest("Ensure fetching with SSH generates the same output"):
      client.succeed(f"{repo.git} push origin-ssh main >&2")
      client.succeed("rm -rf ~/.cache/nix") # Avoid using the cached output of the http fetch

      fetchGit_ssh_expr = f"""
        builtins.fetchGit {{
          url = "{repo.remote_ssh}";
          rev = "{lfs_file_rev}";
          ref = "main";
          lfs = true;
        }}
      """
      fetched_ssh = client.succeed(f"""
        nix eval --debug --impure --raw --expr '({fetchGit_ssh_expr}).outPath'
      """)

      assert fetched_ssh == fetched_lfs, \
        f"fetching with ssh (store path {fetched_ssh}) yielded a different result than using http (store path {fetched_lfs})"
  '';
}
