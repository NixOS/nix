R""(

# Examples

* Remove a package by name:

  ```console
  # nix profile remove hello
  ```

* Remove a package by index
  *(deprecated, will be removed in a future version)*:

  ```console
  # nix profile remove 3
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
