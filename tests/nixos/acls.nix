{ lib, config, nixpkgs, ... }:

let
  pkgs = config.nodes.machine.nixpkgs.pkgs;

  example-package = builtins.toFile "example.nix" ''
    with import <nixpkgs> {};
    stdenvNoCC.mkDerivation {
      name = "example";
      # Check that importing a source works
      exampleSource = builtins.path {
        path = /tmp/bar;
        permissions = {
          protected = true;
          users = ["root"];
        };
      };
      buildCommand = "echo Example > $out; cat $exampleSource >> $out";
      allowSubstitutes = false;
      __permissions = {
        outputs.out = { protected = true; users = ["root"]; };
        drv = { protected = true; users = ["root"]; groups = ["root"]; };
        log.protected = false;
      };
    }
  '';

  test-unaccessible = builtins.toFile "unaccessible.nix" ''
    with import <nixpkgs> {};
    stdenvNoCC.mkDerivation {
      name = "example";
      exampleSource = builtins.path {
        path = /tmp/unaccessible;
        permissions = {
          protected = true;
          users = ["root"];
        };
      };
      buildCommand = "echo Example > $out; cat $exampleSource >> $out";
      allowSubstitutes = false;
      __permissions = {
        outputs.out = { protected = true; users = ["root"]; };
        drv = { protected = true; users = ["root"]; groups = ["root"]; };
        log.protected = false;
      };
    }
  '';
  example-package-diff-permissions = builtins.toFile "example-diff-permissions.nix" ''
    with import <nixpkgs> {};
    stdenvNoCC.mkDerivation {
      name = "example";
      # Check that importing a source works
      exampleSource = builtins.path {
        path = /tmp/bar;
        permissions = {
          protected = true;
          users = ["root" "test"];
          groups = ["root"];
        };
      };
      buildCommand = "echo Example > $out; cat $exampleSource >> $out";
      allowSubstitutes = false;
      __permissions = {
        outputs.out = { protected = true; users = ["root" "test"]; };
        drv = { protected = true; users = [ "root" "test" ]; groups = ["root"]; };
        log.protected = false;
      };
    }
  '';

  example-dependencies = builtins.toFile "example-dependencies.nix" ''
    with import <nixpkgs> {};
    let
      # Check that depending on an already existing but protected package works
      example-package =
        stdenvNoCC.mkDerivation {
          name = "example";
          # Check that importing a source works
          exampleSource = builtins.path {
            path = /tmp/bar;
            permissions = {
              protected = true;
              users = ["root" "test"];
              groups = ["root"];
            };
          };
          buildCommand = "echo Example > $out; cat $exampleSource >> $out";
          allowSubstitutes = false;
          __permissions = {
            outputs.out = { protected = true; users = ["root" "test"]; };

            # At the moment, non trusted user must set permissions which are a superset of existing ones.
            # If some other user adds some permission, this one will become incorrect.
            # Could we declare permissions to add instead of declaring them all ?

            drv = { protected = true; users = ["test" "root"]; groups = ["root"]; };
            log.protected = false;
          };
        };
      example2-package =
        stdenvNoCC.mkDerivation {
          name = "example2";
          buildCommand = "echo Example2 > $out";
          allowSubstitutes = false;
          __permissions = {
            outputs.out = { protected = true; users = ["root" "test"]; };
            drv = { protected = true; users = [ "test" ]; groups = [ "root" ]; };
            log.protected = false;
          };
        }
      ;
      # Check that depending on a new protected package works
      package =
        stdenvNoCC.mkDerivation {
          name = "example3";
          examplePackage = example-package.out;
          exampleSource = example-package.exampleSource;
          examplePackageOther = example2-package;
          buildCommand = "cat $examplePackage $examplePackageOther $exampleSource > $out";
        }
      ;
    in package
  '';

  testInit = ''
    # fmt: off
    import json
    start_all()

    def info(path):
      return json.loads(
        machine.succeed(f"""
          nix store access info --json {path}
        """.strip())
      )

    def assert_info(path, expected, when):
      got = info(path)
      assert(got == expected),f"Path info {got} is not as expected {expected} for path {path} {when}"

    def assert_in_last_line(expected, output):
      last_line = output.splitlines()[-1]
      assert(expected in last_line),f"last line ({last_line}) does not contain string ({expected})"
  '';

 testCli =''
    # fmt: off
    path = machine.succeed(r"""
      nix-build -E '(with import <nixpkgs> {}; runCommand "foo" {} "
        touch $out
      ")'
    """.strip())

    machine.succeed("touch /tmp/bar; chmod 777 /tmp/bar")

    assert_info(path, {"exists": True, "protected": False, "users": [], "groups": []}, "for an empty path")

    machine.succeed(f"""
      nix store access protect {path}
    """)

    assert_info(path, {"exists": True, "protected": True, "users": [], "groups": []}, "after nix store access protect")

    machine.succeed(f"""
      nix store access grant --user root {path}
    """)

    assert_info(path, {"exists": True, "protected": True, "users": ["root"], "groups": []}, "after nix store access grant")

    machine.succeed(f"""
     nix store access grant --group wheel {path}
    """)

    assert_info(path, {"exists": True, "protected": True, "users": ["root"], "groups": ["wheel"]}, "after nix store access grant")

    machine.succeed(f"""
     nix store access revoke --user root --group wheel {path}
    """)

    assert_info(path, {"exists": True, "protected": True, "users": [], "groups": []}, "after nix store access revoke")

    machine.succeed(f"""
      nix store access unprotect {path}
    """)

    assert_info(path, {"exists": True, "protected": False, "users": [], "groups": []}, "after nix store access unprotect")
  '';
  testNonAccessible = ''
    machine.succeed("touch /tmp/unaccessible")
    machine.succeed("chmod 700 /tmp/unaccessible")

    machine.fail("""
     sudo -u test nix store add-file /tmp/unaccessible
    """)

    test_unaccessible_output = machine.fail("""
      sudo -u test nix-build ${test-unaccessible} --no-out-link --debug 2>&1
    """)
    assert_in_last_line("error: opening file '/tmp/unaccessible': Permission denied", test_unaccessible_output)

  '';
  testFoo = ''
    # fmt: off
    machine.succeed("touch foo")

    fooPath = machine.succeed("""
     nix store add-file --protect ./foo
    """).strip()

    assert_info(fooPath, {"exists": True, "protected": True, "users": ["root"], "groups": []}, "after nix store add-file --protect")
'';
  testExamples = ''
    # fmt: off
    examplePackageDrvPath = machine.succeed("""
      nix eval -f ${example-package} --apply "x: x.drvPath" --raw
    """).strip()

    assert_info(examplePackageDrvPath, {"exists": True, "protected": True, "users": ["root"], "groups": ["root"]}, "after nix eval with __permissions")

    examplePackagePath = machine.succeed("""
      nix-build ${example-package}
    """).strip()

    assert_info(examplePackagePath, {"exists": True, "protected": True, "users": ["root"], "groups": []}, "after nix-build with __permissions")

    examplePackagePathDiffPermissions = machine.succeed("""
      sudo -u test nix-build ${example-package-diff-permissions} --no-out-link
    """).strip()

    assert_info(examplePackagePathDiffPermissions, {"exists": True, "protected": True, "users": ["root", "test"], "groups": []}, "after nix-build as a different user")

    assert(examplePackagePath == examplePackagePathDiffPermissions), "Derivation outputs differ when __permissions change"

    machine.succeed(f"""
      nix store access revoke --user test {examplePackagePath}
    """)

    assert_info(examplePackagePath, {"exists": True, "protected": True, "users": ["root"], "groups": []}, "after nix store access revoke")

    machine.succeed(f"""
      nix store access grant --user test {examplePackagePath}
    """)

    assert_info(examplePackagePathDiffPermissions, {"exists": True, "protected": True, "users": ["root", "test"], "groups": []}, "after nix-build as a different user")

    # Trying to revoke permissions fails as a non trusted user.
    try_revoke_output = machine.fail(f"""
      sudo -u test nix store access revoke --user test {examplePackagePath} 2>&1
    """)
    assert_in_last_line("Only trusted users can revoke permissions on path", try_revoke_output)

    exampleDependenciesPackagePath = machine.succeed("""
      sudo -u test nix-build ${example-dependencies} --no-out-link --show-trace
    """).strip()

    print(machine.succeed(f"""
      cat {exampleDependenciesPackagePath}
    """))

    assert_info(exampleDependenciesPackagePath, {"exists": True, "protected": False, "users": [], "groups": []}, "after nix-build with dependencies")
    assert_info(examplePackagePath, {"exists": True, "protected": True, "users": ["root", "test"], "groups": []}, "after nix-build with dependencies")

 '';

  runtime_dep_no_perm = builtins.toFile "runtime_dep_no_perm.nix" ''
    with import <nixpkgs> {};
    stdenvNoCC.mkDerivation {
      name = "example";
      # Check that importing a source works
      exampleSource = builtins.path {
        path = /tmp/dummy;
        permissions = {
          protected = true;
          users = [];
        };
      };
      buildCommand = "echo Example > $out; cat $exampleSource >> $out";
      allowSubstitutes = false;
      __permissions = {
        outputs.out = { protected = true; users = ["test"]; };
        drv = { protected = true; users = ["test"]; };
        log.protected = false;
      };
    }
  '';

 testRuntimeDepNoPermScript = ''
    # fmt: off
    machine.succeed("sudo -u test touch /tmp/dummy")
    output_file = machine.fail("""
      sudo -u test nix-build ${runtime_dep_no_perm} --no-out-link
    """)
 '';

  # A private package only root can access
  private-package = builtins.toFile "private.nix" ''
    with import <nixpkgs> {};
    stdenvNoCC.mkDerivation {
      name = "private";
      privateSource = builtins.path {
        path = /tmp/secret;
        sha256 = "f90af0f74a205cadaad0f17854805cae15652ba2afd7992b73c4823765961533";
        permissions = {
          protected = true;
          users = ["root"];
        };
      };
      buildCommand = "cat $privateSource > $out";
      allowSubstitutes = false;
      __permissions = {
        outputs.out = { protected = true; users = ["root"]; };
        drv = { protected = true; users = ["root"]; groups = ["root"]; };
        log.protected = true;
        log.users = ["root"];
      };
    }
  '';

  # Test depending on a private output, which should fail.
  depend-on-private = builtins.toFile "depend_on_private.nix" ''
    with import <nixpkgs> {};
    let private = import ${private-package}; in
    stdenvNoCC.mkDerivation {
      name = "public";
      buildCommand = "cat ''${private} > $out ";
      allowSubstitutes = false;
      __permissions = {
        outputs.out = { protected = true; users = ["test" "root"]; };
        drv = { protected = true; users = ["test" "root"]; };
        log.protected = true;
        log.users = ["root"];
      };
    }
  '';

  # Test adding a private runtime dependency, which should fail.
  runtime-depend-on-private = builtins.toFile "runtime_depend_on_private.nix" ''
    with import <nixpkgs> {};
    let private = import ${private-package}; in
    stdenvNoCC.mkDerivation {
      name = "public";
      buildCommand = "echo ''${private} > $out ";
      allowSubstitutes = false;
      __permissions = {
        outputs.out = { protected = true; users = ["test"]; };
        drv = { protected = true; users = ["test"]; };
        log.protected = true;
      };
    }
  '';

  # Test depending on a public derivation which depends on a private import
  depend-on-public = builtins.toFile "depend_on_public.nix" ''
    with import <nixpkgs> {};
    let public = import ${depend-on-private}; in
    stdenvNoCC.mkDerivation {
      name = "public";
      buildCommand = "cat ''${public} > $out ";
      allowSubstitutes = false;
      __permissions = {
        outputs.out = { protected = true; users = ["test"]; };
        drv = { protected = true; users = ["test"]; };
        log.protected = true;
        log.users = ["test"];
      };
    }
  '';


  # Only root can access /tmp/secret and the output of the private-package.
  # The `test` user cannot read it nor depend on it in a derivation
  testDependOnPrivate = ''
    # fmt: off
    machine.succeed("""echo "secret_string" > /tmp/secret""");
    machine.succeed("""chmod 700 /tmp/secret""");
    print(machine.succeed("""nix-hash --type sha256 /tmp/secret"""));

    private_output = machine.succeed("""
      sudo nix-build ${private-package} --no-out-link
    """)

    machine.succeed(f"""cat {private_output}""")

    machine.fail(f"""sudo -u test cat {private_output}""")

    depend_on_private_output = machine.fail("""
      sudo -u test nix-build ${depend-on-private} --no-out-link 2>&1
    """)

    # assert_in_last_line("error: test (uid 1000) does not have access to path", depend_on_private_output)

    machine.fail(f"""sudo -u test cat {private_output}""")
    runtime_depend_on_private_output = machine.fail("""
      sudo -u test nix-build ${runtime-depend-on-private} --no-out-link 2>&1
    """)
    # assert_in_last_line("error: test (uid 1000) does not have access to path", runtime_depend_on_private_output)

    machine.fail(f"""sudo -u test cat {private_output}""")

    # Root builds the derivation to give access to test
    public_output = machine.succeed("""
      sudo nix-build ${depend-on-private} --no-out-link
    """)

    print(machine.succeed(f"""sudo -u test cat {public_output}"""))
    print(machine.succeed(f"""getfacl {public_output}"""))
    print(machine.succeed(f"""getfacl {private_output}"""))

    machine.fail(f"""sudo -u test cat {private_output}""")

    machine.succeed(f"""sudo -u test cat {public_output}""")

    # Test can depend on the values that were made public, even if it these have private build time dependencies.
    machine.succeed("""
      sudo -u test nix-build ${depend-on-public} --no-out-link
    """)
  '';

  # Non trusted user gives permission to another one.
  test-user-private = builtins.toFile "test-user-private.nix" ''
    with import <nixpkgs> {};
    stdenvNoCC.mkDerivation {
      name = "test-user-private";
      privateSource = builtins.path {
        path = /tmp/test_secret;
        sha256 = "f90af0f74a205cadaad0f17854805cae15652ba2afd7992b73c4823765961533";
        permissions = {
          protected = true;
          users = ["test"];
        };
      };
      buildCommand = "cat $privateSource > $out && echo Example >> $out";
      allowSubstitutes = false;
      __permissions = {
        outputs.out = { protected = true; users = ["test" "test2"]; };
        drv = { protected = true; users = ["test" "test2"]; };
        log.protected = true;
        log.users = ["test" "test2"];
      };
    }
  '';

  test-user-private-2 = builtins.toFile "test-user-private-2.nix" ''
    with import <nixpkgs> {};
    stdenvNoCC.mkDerivation {
      name = "test-user-private";
      privateSource = builtins.path {
        path = /tmp/test_secret;
        sha256 = "f90af0f74a205cadaad0f17854805cae15652ba2afd7992b73c4823765961533";
        permissions = {
          protected = true;
          users = ["test" "test2"];
        };
      };
      buildCommand = "echo $privateSource > $out && echo Example >> $out";
      allowSubstitutes = false;
      __permissions = {
        outputs.out = { protected = true; users = ["test" "test2" "test3"]; };
        drv = { protected = true; users = ["test" "test2"]; };
        log.protected = true;
        log.users = ["test" "test2"];
      };
    }
  '';

  # Non trusted user grants access to its private file
  testTestUserPrivate = ''
    # fmt: off
    machine.succeed("""sudo -u test bash -c 'echo secret_string > /tmp/test_secret'""");
    machine.succeed("""sudo -u test chmod 700 /tmp/test_secret""");

    # User test2 cannot build the derivation itself
    assert_in_last_line(
      "you (test2) would not have access to path /nix/store/lh7vw9n09hf41sqq8pb7vwb7kyq8f6da-test_secret",
      machine.fail("""
        sudo -u test2 nix-build ${test-user-private} --no-out-link 2>&1
      """)
    )

    # User test can do it to grant access to the outputs to test2
    userPrivatePath = machine.succeed("""
     sudo -u test nix-build ${test-user-private} --no-out-link
    """)

    # Since the derivation is already built, test2 could now run the build command with no effect
    # It could also build a new derivation that depend on the public outputs.
    machine.succeed("""
     sudo -u test2 nix-build ${test-user-private} --no-out-link
    """)

    assert_info(userPrivatePath, {"exists": True, "protected": True, "users": ["test", "test2"], "groups": []}, "after nix-build test-user-private")

    machine.succeed(f"""
      sudo -u test2 cat {userPrivatePath}
    """)

    # Non trusted user cannot revoke permissions, even if it was the one who granted them.
    machine.fail(f"""
     sudo -u test nix store access revoke --user test2 {userPrivatePath}
    """)

    testUserPrivateDrv = machine.succeed("""
        sudo -u test nix-instantiate ${test-user-private} 
    """).strip()
    testUserPrivateInput=machine.succeed(f"nix-store -q --references {testUserPrivateDrv} | grep test_secret").strip()

    # Since test2 was given permissions, it can grant access to test3
    machine.succeed(f"""
     sudo -u test2 nix store access grant --user test3 {userPrivatePath}
    """)

    # test2 cannot add itself to the permissions of /tmp/test_secret
    assert_in_last_line(
      "Could not access file (/tmp/test_secret) permissions may be missing",
      machine.fail("""
      sudo -u test2 nix-build ${test-user-private-2} --no-out-link 2>&1
      """)
    )

    assert_in_last_line(
      "test_secret: Permission denied",
      machine.fail(f"""
      sudo -u test2 cat {testUserPrivateInput} 2>&1
      """)
    )

  '';

  # Tests importing a private folder
  test-import-folder = builtins.toFile "test-import-folder.nix" ''
    with import <nixpkgs> {};
    stdenvNoCC.mkDerivation {
      name = "test-import-folder";
      outputs = ["out" "out2"];
      privateSource = builtins.path {
        path = /tmp/private-src;
        sha256 = "961102b8a00318065d49b8c941adc13f56da0fbb56e094de4917b6fdf80a41df";
        permissions = {
          protected = true;
          users = ["test" "test3"];
        };
      };
      buildCommand = "touch $out2 && mkdir $out && cat $privateSource/1 $privateSource/2 > $out/output";
      allowSubstitutes = false;
      __permissions = {
        outputs.out = { protected = true; users = ["test" "test2"]; };
        outputs.out2 = { protected = true; users = ["test" "test2"]; };
        drv = { protected = true; users = ["test" "test2"]; };
        log.protected = true;
        log.users = ["test" "test2"];
      };
    }
  '';

  # Tests overriding permissions over a private folder that was previsously imported.
  test-import-folder-2 = builtins.toFile "test-import-folder-2.nix" ''
    with import <nixpkgs> {};
    stdenvNoCC.mkDerivation {
      name = "test-import-folder";
      outputs = ["out" "out2"];
      privateSource = builtins.path {
        path = /tmp/private-src;
        sha256 = "961102b8a00318065d49b8c941adc13f56da0fbb56e094de4917b6fdf80a41df";
        permissions = {
          protected = true;
          users = ["test" "test2"];
        };
      };
      buildCommand = "touch $out2 && mkdir $out && cat $privateSource/1 $privateSource/2 > $out/output";
      allowSubstitutes = false;
      __permissions = {
        outputs.out = { protected = true; users = ["test" "test2"]; };
        outputs.out2 = { protected = true; users = ["test" "test2"]; };
        drv = { protected = true; users = ["test" "test2"]; };
        log.protected = true;
        log.users = ["test" "test2"];
      };
    }
  '';

  # Tests importing a private folder
  testImportFolder = ''
    # fmt: off
    machine.succeed("""sudo -u test mkdir /tmp/private-src""")
    machine.succeed("""sudo -u test bash -c 'echo secret_string_1 > /tmp/private-src/1'""")
    machine.succeed("""sudo -u test bash -c 'echo secret_string_2 > /tmp/private-src/2'""")
    machine.succeed("""sudo -u test chmod 700 /tmp/private-src/2""")

    # test2 does not have access to all the files in /tmp/private-src
    machine.fail("""
        sudo -u test2 nix-build ${test-import-folder} --no-out-link 2>&1
    """)

    # test has read access to all the files in /tmp/private-src
    testImportFolderPath = machine.succeed("""
        sudo -u test nix-build ${test-import-folder} --no-out-link
    """).strip()

    assert_info(f"""{testImportFolderPath}/output""", {"exists": True, "protected": True, "users": ["test", "test2"], "groups": []}, "after nix-build test-import-folder")

    # Check permissions of the copies of the files from /tmp/private-src in the store
    testImportFolderPathDrv = machine.succeed("""
        sudo -u test nix-instantiate ${test-import-folder}
    """).strip()
    inputFolderPath=machine.succeed(f"nix-store -q --references {testImportFolderPathDrv} | grep private-src").strip()
    assert_info(f"""{inputFolderPath}""", {"exists": True, "protected": True, "users": ["test", "test3"], "groups": []}, "inputFolderPath")
    assert_info(f"""{inputFolderPath}/1""", {"exists": True, "protected": True, "users": ["test", "test3"], "groups": []}, "inputFolderPath/1")
    assert_info(f"""{inputFolderPath}/2""", {"exists": True, "protected": True, "users": ["test", "test3"], "groups": []}, "inputFolderPath/2")

    # test2 tries grant itself permission to the /tmp/private-src input but it cannot read all the original files
    assert_in_last_line(
      "Could not access file (/tmp/private-src/2) permissions may be missing",
      machine.fail("""
        sudo -u test2 nix-build ${test-import-folder-2} --no-out-link 2>&1
      """)
    )

    # test2 can now read all the files in /tmp/private-src
    machine.succeed("""sudo -u test chmod 777 /tmp/private-src/2""")

    # test-import-folder-2 will remove the permissions of test3 so this fails
    assert_in_last_line(
      f"Only trusted users can revoke permissions on path {inputFolderPath}",
      machine.fail("""
        sudo -u test2 nix-build ${test-import-folder-2} --no-out-link 2>&1
      """)
    )

    # It succeeds after a trusted user manually removes the permissions of test3
    machine.succeed(f"nix store access revoke {inputFolderPath} --user test3")
    machine.succeed("""
        sudo -u test2 nix-build ${test-import-folder-2} --no-out-link
    """)
  '';

  private-flake = builtins.toFile "private-flake.nix" ''
    {
      description = "Test alcs with flake";
      outputs = { self, nixpkgs }: {
        packages.x86_64-linux.default =
          with import nixpkgs { system = "x86_64-linux"; };
          stdenvNoCC.mkDerivation {
            name = "private-flake";
            src = builtins.path {
              path = self;
              permissions = {
                protected = true;
                users = ["root"];
              };
            };
            buildCommand = "echo Example > $out && cat $src/secret >> $out";
            __permissions = {
              outputs.out = { protected = true; users = ["test" "test2" "root"]; };
              drv = { protected = true; users = ["test" "test2" "root"]; };
              log.protected = true;
              log.users = ["test" "test2" "root"];
            };
          };
      };
    }
  '';

  public-flake = builtins.toFile "public-flake.nix" ''
    {
      description = "Test alcs with flake";
      inputs.priv.url = "/tmp/private-flake";
      outputs = { self, nixpkgs, priv }: {
        packages.x86_64-linux.default =
          with import nixpkgs { system = "x86_64-linux"; };
          stdenvNoCC.mkDerivation {
            name = "public-flake";
            src = self;
            buildCommand = "echo Example > $out && cat ''${priv.packages.x86_64-linux.default} >> $out";
            __permissions = {
              outputs.out = { protected = true; users = ["test" "test2"]; };
              drv = { protected = true; users = ["test" "test2"]; };
              log.protected = true;
              log.users = ["test" "test2"];
            };
          };
      };
    }
  '';

  testFlake = ''
    # fmt: off
    machine.succeed("mkdir /tmp/private-flake")
    machine.succeed("echo secret_string > /tmp/private-flake/secret && chmod 700 /tmp/private-flake/secret")
    machine.succeed("cp ${private-flake} /tmp/private-flake/flake.nix")
    assert_in_last_line(
      "error: opening file '/tmp/private-flake/secret': Permission denied",
      machine.fail("cd /tmp/private-flake && sudo -u test nix build --print-out-paths 2>&1")
    )
    flakeOutput = machine.succeed("cd /tmp/private-flake && nix build --print-out-paths --no-link")
    assert_info(f"""{flakeOutput}""", {"exists": True, "protected": True, "users": ["root", "test", "test2"], "groups": []}, "inputFolderPath")

    # public-flake depends on the public output of private-flake
    machine.succeed("mkdir /tmp/public-flake && cp ${public-flake} /tmp/public-flake/flake.nix")

    # We build the lockfile as root because this needs to check that the secret input did not change since the build
    machine.succeed("cd /tmp/public-flake &&  nix flake lock")

    # After this the test user can rely on the output of the already built private-flake
    publicFlakeOutput = machine.succeed("cd /tmp/public-flake && sudo -u test nix build --print-out-paths --no-link")
    assert_info(f"""{publicFlakeOutput}""", {"exists": True, "protected": True, "users": ["test", "test2"], "groups": []}, "inputFolderPath")
  '';

in
{
  name = "acls";

  nodes.machine =
    { config, lib, pkgs, ... }:
    { virtualisation.writableStore = true;
      nix.settings.substituters = lib.mkForce [ ];
      nix.settings.experimental-features = lib.mkForce [ "nix-command" "acls" "flakes"];

      # Do not try to download the registry and setup a local nixpkgs for flake tests
      nix.settings.flake-registry = builtins.toFile "global-registry.json" ''{"flakes":[],"version":2}'';
      nix.registry = {
          nixpkgs = {
            from = {
              type = "indirect";
              id = "nixpkgs";
            };
            to = {
              type = "path";
              path = "${nixpkgs}";
            };
          };
        };
      nix.nixPath = [ "nixpkgs=${lib.cleanSource pkgs.path}" ];
      nix.checkConfig = false;
      virtualisation.additionalPaths = [ pkgs.stdenvNoCC pkgs.pkgsi686Linux.stdenvNoCC ];
      users.users.test = {
        isNormalUser = true;
      };
      users.users.test2 = {
        isNormalUser = true;
      };
      users.users.test3 = {
        isNormalUser = true;
      };
    };

  testScript = { nodes }: testInit + lib.strings.concatStrings
    [
      testCli
      testNonAccessible
      testFoo
      testExamples
      testDependOnPrivate
      testTestUserPrivate
      testImportFolder
      testRuntimeDepNoPermScript
      testFlake
    ];
}
