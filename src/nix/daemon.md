R""(

# Example

* Run the daemon in the foreground:

  ```console
  # nix daemon
  ```

* The daemon does not have native support for listening on a TCP
  socket, but you can do this using `socat`:

  ```console
  # socat TCP-LISTEN:3456,reuseaddr,fork EXEC:'nix daemon --stdio',nofork
  ```

  You can then connect to this daemon using the `tcp` store type:

  ```console
  # nix store ping --store tcp://example.org:3456
  ```

# Description

This command runs the Nix daemon, which is a required component in
multi-user Nix installations. It performs build actions and other
operations on the Nix store on behalf of non-root users. Usually you
don't run the daemon directly; instead it's managed by a service
management framework such as `systemd`.

Note that this daemon does not fork into the background.

)""
