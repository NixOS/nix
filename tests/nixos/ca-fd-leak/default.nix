# Nix is a sandboxed build system. But Not everything can be handled inside its
# sandbox: Network access is normally blocked off, but to download sources, a
# trapdoor has to exist. Nix handles this by having "Fixed-output derivations".
# The detail here is not important, but in our case it means that the hash of
# the output has to be known beforehand. And if you know that, you get a few
# rights: you no longer run inside a special network namespace!
#
# Now, Linux has a special feature, that not many other unices do: Abstract
# unix domain sockets! Not only that, but those are namespaced using the
# network namespace! That means that we have a way to create sockets that are
# available in every single fixed-output derivation, and also all processes
# running on the host machine! Now, this wouldn't be that much of an issue, as,
# well, the whole idea is that the output is pure, and all processes in the
# sandbox are killed before finalizing the output. What if we didn't need those
# processes at all? Unix domain sockets have a semi-known trick: you can pass
# file descriptors around!
# This makes it possible to exfiltrate a file-descriptor with write access to
# $out outside of the sandbox. And that file-descriptor can be used to modify
# the contents of the store path after it has been registered.

{ config, ... }:

let
  pkgs = config.nodes.machine.nixpkgs.pkgs;

  # Simple C program that sends a a file descriptor to `$out` to a Unix
  # domain socket.
  # Compiled statically so that we can easily send it to the VM and use it
  # inside the build sandbox.
  sender = pkgs.runCommandWith {
    name = "sender";
    stdenv = pkgs.pkgsStatic.stdenv;
  } ''
    $CC -static -o $out ${./sender.c}
  '';

  # Okay, so we have a file descriptor shipped out of the FOD now. But the
  # Nix store is read-only, right? .. Well, yeah. But this file descriptor
  # lives in a mount namespace where it is not! So even when this file exists
  # in the actual Nix store, we're capable of just modifying its contents...
  smuggler = pkgs.writeCBin "smuggler" (builtins.readFile ./smuggler.c);

  # The abstract socket path used to exfiltrate the file descriptor
  socketName = "FODSandboxExfiltrationSocket";
in
{
  name = "ca-fd-leak";

  nodes.machine =
    { config, lib, pkgs, ... }:
    { virtualisation.writableStore = true;
      nix.settings.substituters = lib.mkForce [ ];
      virtualisation.additionalPaths = [ pkgs.busybox-sandbox-shell sender smuggler pkgs.socat ];
    };

  testScript = { nodes }: ''
    start_all()

    machine.succeed("echo hello")
    # Start the smuggler server
    machine.succeed("${smuggler}/bin/smuggler ${socketName} >&2 &")

    # Build the smuggled derivation.
    # This will connect to the smuggler server and send it the file descriptor
    machine.succeed(r"""
      nix-build -E '
        builtins.derivation {
          name = "smuggled";
          system = builtins.currentSystem;
          # look ma, no tricks!
          outputHashMode = "flat";
          outputHashAlgo = "sha256";
          outputHash = builtins.hashString "sha256" "hello, world\n";
          builder = "${pkgs.busybox-sandbox-shell}/bin/sh";
          args = [ "-c" "echo \"hello, world\" > $out; ''${${sender}} ${socketName}" ];
      }'
    """.strip())


    # Tell the smuggler server that we're done
    machine.execute("echo done | ${pkgs.socat}/bin/socat - ABSTRACT-CONNECT:${socketName}")

    # Check that the file was not modified
    machine.succeed(r"""
      cat ./result
      test "$(cat ./result)" = "hello, world"
    """.strip())
  '';

}
