R""(

# Example

* Run the daemon in the foreground:

  ```console
  # nix daemon
  ```

# Description

This command runs the Nix daemon, which is a required component in
multi-user Nix installations. It performs build actions and other
operations on the Nix store on behalf of non-root users. Usually you
don't run the daemon directly; instead it's managed by a service
management framework such as `systemd`.

Note that this daemon does not fork into the background.

)""
