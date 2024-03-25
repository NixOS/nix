# Using Nix in multi-user mode with a non-root daemon

> Experimental blurb

It is experimentally possible to run Nix in multi-user mode without running the whole daemon as root.
This is done by delegating the only part that requires root access to a separate daemon, with a much smaller attack surface.
Because of the need for a second daemon, this makes the setup a bit more complex and isn't yet supported by the installer. It is however possible to set this up manually:

1. Create a new user and group for the daemon:
    ```sh
    sudo groupadd nix-daemon
    sudo useradd --gid nix-daemon --system -c "Nix daemon user" nix-daemon
    ```
2. Create `/nix` owned by that user:
    ```sh
    sudo mkdir -m 0755 /nix
    sudo chown nix-daemon:nix-daemon /nix
    ```
3. Download a statically-compiled Nix version for bootstrapping
    ```sh
    curl -L https://hydra.nixos.org/job/nix/master/buildStatic.x86_64-linux/latest/download-by-type/file/binary-dist -o /tmp/nix-env
    chmod +x /tmp/nix-env
    ```
4. Install a proper Nix and the tracing daemon in the store
    ```sh
    export DAEMON_HOME=$(sudo -u nix-daemon mktemp -d)
    sudo -u nix-daemon HOME="$DAEMON_HOME" \
        /tmp/nix-env \
        -f https://github.com/nixos/nix/archive/rootless-daemon.tar.gz \
        -iA default packages.x86_64-linux.nix-find-roots \
        --option extra-substituters https://nixos-nix-install-tests.cachix.org \
        --option extra-trusted-public-keys nixos-nix-install-tests.cachix.org-1:Le57vOUJjOcdzLlbwmZVBuLGoDC+Xg2rQDtmIzALgFU= \
        --store / \
        --profile /nix/var/nix/profiles/default
    sudo -u nix-daemon mkdir -p /nix/var/nix/gc-socket
    sudo -u nix-daemon rm -rf "$DAEMON_HOME"
    ```
5. Move the tracing daemon executable out of the store (as we don't want Nix
   to own it)
   ```sh
   sudo cp /nix/var/nix/profiles/default/bin/nix-find-roots /usr/bin/
   ```
6. Install the systemd services for the daemon:
    ```sh
    cat <<EOF | sudo tee /etc/systemd/system/nix-daemon.service
    [Unit]
    Description=Nix Daemon
    Documentation=man:nix-daemon https://nixos.org/manual
    RequiresMountsFor=/nix/store
    RequiresMountsFor=/nix/var
    RequiresMountsFor=/nix/var/nix/db
    ConditionPathIsReadWrite=/nix/var/nix/daemon-socket

    [Service]
    ExecStart=@/nix/var/nix/profiles/default/bin/nix-daemon nix-daemon --daemon
    KillMode=process
    LimitNOFILE=1048576
    TasksMax=1048576
    User=nix-daemon
    Group=nix-daemon

    [Install]
    WantedBy=multi-user.target
    EOF
    ```
    ```sh
    cat <<EOF | sudo tee /etc/systemd/system/nix-daemon.socket
    [Unit]
    Description=Nix Daemon Socket
    Before=multi-user.target
    RequiresMountsFor=/nix/store
    ConditionPathIsReadWrite=/nix/var/nix/daemon-socket

    [Socket]
    ListenStream=/nix/var/nix/daemon-socket/socket
    SocketUser=nix-daemon

    [Install]
    WantedBy=sockets.target
    EOF
    ```
7. Install the systemd services for the tracing daemon:
    ```sh
    cat <<EOF | sudo tee /etc/systemd/system/nix-find-roots.service
    [Unit]
    Description=Nix GC tracer daemon
    RequiresMountsFor=/nix/store
    RequiresMountsFor=/nix/var
    ConditionPathIsReadWrite=/nix/var/nix/gc-socket
    ProcSubset=pid

    [Service]
    ExecStart=@/usr/bin/nix-find-roots nix-find-roots
    Type=simple
    StandardError=journal
    ProtectSystem=full
    ReadWritePaths=/nix/var/nix/gc-socket
    SystemCallFilter=@system-service
    SystemCallErrorNumber=EPERM
    PrivateNetwork=true
    PrivateDevices=true
    ProtectKernelTunables=true

    [Install]
    WantedBy=multi-user.target
    EOF
    ```
    ```sh
    cat <<EOF | sudo tee /etc/systemd/system/nix-find-roots.socket
    [Unit]
    Description=Nix Daemon Socket
    Before=multi-user.target
    RequiresMountsFor=/nix/store
    ConditionPathIsReadWrite=/nix/var/nix/gc-socket

    [Socket]
    ListenStream=/nix/var/nix/gc-socket/socket
    Accept=false

    [Install]
    WantedBy=sockets.target
    EOF
    ```
8. Enable the required experimental Nix feature and basic configuration:
    ```sh
    sudo mkdir /etc/nix
    cat <<EOF | sudo tee /etc/nix/nix.conf
    experimental-features = external-gc-daemon
    trusted-public-keys = cache.nixos.org-1:6NCHdD59X431o0gWypbMrAURkbJ16ZPMQFGspcDShjY=
    substituters = https://cache.nixos.org/
    EOF
    ```
9. Start the systemd sockets:
    ```sh
    sudo systemctl start nix-daemon.socket
    sudo systemctl start nix-find-roots.socket
    ```
10. Profit
