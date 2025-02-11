{ config, ... }:

let
  pkgs = config.nodes.machine.nixpkgs.pkgs;

  attacker =
    pkgs.runCommandWith
      {
        name = "attacker";
        stdenv = pkgs.pkgsStatic.stdenv;
      }
      ''
        $CC -static -o $out ${./attacker.c}
      '';

  try-open-build-dir = pkgs.writeScript "try-open-build-dir" ''
    export PATH=${pkgs.coreutils}/bin:$PATH

    set -x

    chmod 700 .
    # Shouldn't be able to open the root build directory
    (! chmod 700 ..)

    touch foo

    # Synchronisation point: create a world-writable fifo and wait for someone
    # to write into it
    mkfifo syncPoint
    chmod 777 syncPoint
    cat syncPoint

    touch $out

    set +x
  '';

  create-hello-world = pkgs.writeScript "create-hello-world" ''
    export PATH=${pkgs.coreutils}/bin:$PATH

    set -x

    echo "hello, world" > result

    # Synchronisation point: create a world-writable fifo and wait for someone
    # to write into it
    mkfifo syncPoint
    chmod 777 syncPoint
    cat syncPoint

    cp result $out

    set +x
  '';

in
{
  name = "sandbox-setuid-leak";

  nodes.machine =
    {
      config,
      lib,
      pkgs,
      ...
    }:
    {
      virtualisation.writableStore = true;
      nix.settings.substituters = lib.mkForce [ ];
      nix.nrBuildUsers = 1;
      virtualisation.additionalPaths = [
        pkgs.busybox-sandbox-shell
        attacker
        try-open-build-dir
        create-hello-world
        pkgs.socat
      ];
      boot.kernelPackages = pkgs.linuxPackages_latest;
      users.users.alice = {
        isNormalUser = true;
      };
    };

  testScript =
    { nodes }:
    ''
      start_all()

      with subtest("A builder can't give access to its build directory"):
          # Make sure that a builder can't change the permissions on its build
          # directory to the point of opening it up to external users

          # A derivation whose builder tries to make its build directory as open
          # as possible and wait for someone to hijack it
          machine.succeed(r"""
            nix-build -v -E '
              builtins.derivation {
                name = "open-build-dir";
                system = builtins.currentSystem;
                builder = "${pkgs.busybox-sandbox-shell}/bin/sh";
                args = [ (builtins.storePath "${try-open-build-dir}") ];
            }' >&2 &
          """.strip())

          # Wait for the build to be ready
          # This is OK because it runs as root, so we can access everything
          machine.wait_for_file("/tmp/nix-build-open-build-dir.drv-0/build/syncPoint")

          # But Alice shouldn't be able to access the build directory
          machine.fail("su alice -c 'ls /tmp/nix-build-open-build-dir.drv-0/build'")
          machine.fail("su alice -c 'touch /tmp/nix-build-open-build-dir.drv-0/build/bar'")
          machine.fail("su alice -c 'cat /tmp/nix-build-open-build-dir.drv-0/build/foo'")

          # Tell the user to finish the build
          machine.succeed("echo foo > /tmp/nix-build-open-build-dir.drv-0/build/syncPoint")

      with subtest("Being able to execute stuff as the build user doesn't give access to the build dir"):
          machine.succeed(r"""
            nix-build -E '
              builtins.derivation {
                name = "innocent";
                system = builtins.currentSystem;
                builder = "${pkgs.busybox-sandbox-shell}/bin/sh";
                args = [ (builtins.storePath "${create-hello-world}") ];
            }' >&2 &
          """.strip())
          machine.wait_for_file("/tmp/nix-build-innocent.drv-0/build/syncPoint")

          # The build ran as `nixbld1` (which is the only build user on the
          # machine), but a process running as `nixbld1` outside the sandbox
          # shouldn't be able to touch the build directory regardless
          machine.fail("su nixbld1 --shell ${pkgs.busybox-sandbox-shell}/bin/sh -c 'ls /tmp/nix-build-innocent.drv-0/build'")
          machine.fail("su nixbld1 --shell ${pkgs.busybox-sandbox-shell}/bin/sh -c 'echo pwned > /tmp/nix-build-innocent.drv-0/build/result'")

          # Finish the build
          machine.succeed("echo foo > /tmp/nix-build-innocent.drv-0/build/syncPoint")

          # Check that the build was not affected
          machine.succeed(r"""
            cat ./result
            test "$(cat ./result)" = "hello, world"
          """.strip())
    '';

}
