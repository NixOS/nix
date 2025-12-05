# Test Nix's remote build feature with host-to-VM socket forwarding.
# This tests that the host (test driver) can perform remote builds in a VM
# using a socket connection, demonstrating the socket-only daemon functionality.

{
  config,
  hostPkgs,
  ...
}:

let
  # TCP port for the Nix daemon inside the VM
  daemonPort = 3049;

  # The configuration of the VM builder.
  builder =
    {
      config,
      pkgs,
      lib,
      ...
    }:
    {
      environment.systemPackages = [ pkgs.netcat ];
      virtualisation.writableStore = true;
      nix.settings.sandbox = true;

      # Forward TCP port from host to guest
      # We'll use socat on the host to bridge Unix socket → localhost TCP
      # QEMU forwards localhost:daemonPort → guest:10.0.2.15:daemonPort
      # Note: QEMU will support Unix socket forwarding natively (hostfwd=unix:...)
      # once https://gitlab.com/qemu-project/qemu/-/commit/6d10e021318b16e3e90f98b7b2fa187826e26c0a
      # is released, which would eliminate the need for socat
      virtualisation.forwardPorts = [
        {
          from = "host";
          host.port = daemonPort;
          guest.port = daemonPort;
        }
      ];

      # Configure nix-daemon to listen on TCP directly
      # Empty string clears the default Unix socket, then we add TCP
      # Note: We bind to the eth0 IP address. In QEMU user networking, this is typically
      # 10.0.2.15 but we use FreeBind to allow binding before the network is fully configured.
      # Binding to a specific IP (not 0.0.0.0) prevents listening on localhost.
      # NOTE: This is similar to what is proposed in https://github.com/systemd/systemd/issues/32795
      # (using a separate network interface only accessible to systemd), but that's not yet
      # implemented.
      systemd.sockets.nix-daemon = {
        listenStreams = [
          ""
          # QEMU user networking assigns 10.0.2.15 by default
          "10.0.2.15:${toString daemonPort}"
        ];
        # FreeBind allows binding to IPs that don't exist yet
        socketConfig = {
          FreeBind = true;
        };
      };

      # Restrict access to the daemon port: only allow connections from QEMU gateway
      # In QEMU user networking, forwarded connections appear to come from the gateway (10.0.2.2)
      # This should prevent unprivileged guest processes from accessing the daemon.
      # For production use, consider additional isolation mechanisms (see systemd.sockets comment above).
      # This has not been audited.
      networking.firewall.extraCommands = ''
        # Insert in reverse order since -I inserts at position 1
        # Drop all connections to daemon port (inserted first, will be at position 2)
        iptables -I nixos-fw -p tcp --dport ${toString daemonPort} -j nixos-fw-log-refuse
        # Allow connections from QEMU gateway only (inserted second, will be at position 1)
        iptables -I nixos-fw -p tcp --dport ${toString daemonPort} -s 10.0.2.2 -j nixos-fw-accept
      '';
    };

in

{
  config = {
    name = "remote-builds-plain-daemon";

    nodes = {
      builder = builder;
    };

    testScript =
      { nodes }:
      ''
        # fmt: off
        import subprocess
        import os
        import time

        start_all()

        # Wait for the VM to be ready
        builder.wait_for_unit("nix-daemon.socket")

        # Verify the daemon is listening on TCP
        builder.succeed("ss -tlnp | grep ${toString daemonPort}")

        print("VM builder is ready with TCP daemon")

        # Start socat to bridge Unix socket → localhost TCP
        # QEMU forwards localhost:daemonPort → VM's 10.0.2.15:daemonPort
        # (See virtualisation.forwardPorts comment for future QEMU native Unix socket support)
        socket_path = os.environ.get('TMPDIR', '/tmp') + '/nix-builder.sock'

        print(f"Starting socat to forward {socket_path} -> localhost:${toString daemonPort}")
        socat_proc = subprocess.Popen(
            ["${hostPkgs.socat}/bin/socat",
             f"UNIX-LISTEN:{socket_path},fork",
             "TCP:127.0.0.1:${toString daemonPort}"],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE
        )

        # Wait for socket to be created
        for i in range(30):
            if os.path.exists(socket_path):
                break
            if socat_proc.poll() is not None:
                stdout, stderr = socat_proc.communicate()
                raise Exception(f"socat died unexpectedly: {stderr.decode()}")
            time.sleep(0.1)
        else:
            socat_proc.terminate()
            raise Exception(f"Socket {socket_path} was not created by socat")

        print(f"Host socket {socket_path} ready (socat -> QEMU -> VM)")

        # Test the connection by trying a simple operation
        print("Testing connection to VM daemon...")
        subprocess.run(
            ["${hostPkgs.nix}/bin/nix",
             "--extra-experimental-features", "nix-command",
             "--store", f"unix://{socket_path}",
             "store", "ping"],
            check=True,
            timeout=60
        )

        # Create a simple derivation to build
        #  Use a fixed system instead of builtins.currentSystem
        test_expr = """
        derivation {
          name = "socket-forward-host-build-test";
          system = "x86_64-linux";
          builder = "/bin/sh";
          args = [ "-c" "echo 'Built via forwarded socket from host!' > $out" ];
        }
        """

        expr_file = os.environ.get('TMPDIR', '/tmp') + '/test-expr.nix'
        with open(expr_file, 'w') as f:
            f.write(test_expr)

        # Create a fresh store for the host
        host_store = os.environ.get('TMPDIR', '/tmp') + '/host-store'
        os.makedirs(host_store, exist_ok=True)

        # Perform a build from the host using the VM as a builder
        # Builders format: <uri> <system> <ssh-key> <max-jobs> <speed-factor> <features> <mandatory-features>
        print("Host performing remote build in VM via socket->TCP bridge...")
        result = subprocess.run(
            ["${hostPkgs.nix}/bin/nix-build",
             "--store", host_store,
             expr_file,
             "--no-out-link",
             "--option", "builders", f"unix://{socket_path} x86_64-linux - 1",
             "--option", "require-sigs", "false",
             "--max-jobs", "0"],  # Force remote building
            stdout=subprocess.PIPE,
            text=True
        )

        if result.returncode != 0:
            print(f"Build failed with exit code {result.returncode}")
            raise Exception("Build failed")

        out_path = result.stdout.strip()
        print(f"Build succeeded! Output: {out_path}")

        # Verify the build happened in the VM by checking it exists there
        builder.succeed(f"test -e {out_path}")

        # Verify the build output was copied to the host's physical store
        host_physical_path = f"{host_store}{out_path}"
        print(f"Checking host physical store at: {host_physical_path}")
        if not os.path.exists(host_physical_path):
            raise Exception(f"Build output not found in host store: {host_physical_path}")

        # Verify the build output content
        with open(host_physical_path, 'r') as f:
            content = f.read()
        expected = "Built via forwarded socket from host!\n"
        if content != expected:
            raise Exception(f"Build output has wrong content. Expected: {repr(expected)}, got: {repr(content)}")

        print("Socket-forwarded remote build from host test PASSED")


        # Test that guest processes CANNOT connect (firewall enabled)
        print("Testing that guest user CANNOT connect to daemon port (firewall enabled)...")
        builder.fail("timeout 5 nc -z 127.0.0.1 ${toString daemonPort}")
        print("Confirmed: guest user cannot connect to localhost")

        builder.fail("timeout 5 nc -z 10.0.2.15 ${toString daemonPort}")
        print("Confirmed: guest user cannot connect to interface IP")

        # Clean up socat
        print("Cleaning up socat...")
        socat_proc.terminate()
        socat_proc.wait(timeout=5)
      '';
  };
}
