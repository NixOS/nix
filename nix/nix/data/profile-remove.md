R""(

# Examples

* Remove a package by position:

  ```console
  # nix profile remove 3
  ```

* Remove a package by attribute path:

  ```console
  # nix profile remove packages.x86_64-linux.hello
  ```

* Remove all packages:

  ```console
  # nix profile remove '.*'
  ```

* Remove a package by store path:

  ```console
  # nix profile remove /nix/store/rr3y0c6zyk7kjjl8y19s4lsrhn4aiq1z-hello-2.10
  ```

# Description

This command removes a package from a profile.

)""
