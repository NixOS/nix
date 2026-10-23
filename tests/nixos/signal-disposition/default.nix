# This test verifies that no signals have the SIG_IGN disposition inside a nix builder.
#
# systemd sets SIG_IGN for SIGPIPE on all services (IgnoreSIGPIPE=yes),
# and other service managers or parent processes could ignore other signals.
# Ignored dispositions survive fork() and execve(), so builders inherit
# them unless `commonChildInit()` explicitly resets them to SIG_DFL.

{ config, ... }:

let
  pkgs = config.nodes.machine.nixpkgs.pkgs;

  # Compiled statically so it works inside the build sandbox without
  # needing the dynamic linker in the sandbox closure.
  checker =
    pkgs.runCommandWith
      {
        name = "check-signals";
        stdenv = pkgs.pkgsStatic.stdenv;
      }
      ''
        $CC -static -o $out ${./check-signals.c}
      '';
in

{
  name = "signal-disposition";

  nodes.machine = {
    virtualisation.writableStore = true;
    virtualisation.additionalPaths = [ checker ];
  };

  testScript = ''
    machine.wait_for_unit("multi-user.target")

    machine.succeed(r"""
      nix-build -E '
        derivation {
          name = "check-signals";
          system = builtins.currentSystem;
          builder = builtins.storePath "${checker}";
        }'
    """.strip())
  '';
}
