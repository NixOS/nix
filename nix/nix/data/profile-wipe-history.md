R""(

# Examples

* Delete all versions of the default profile older than 100 days:

  ```console
  # nix profile wipe-history --profile /tmp/profile --older-than 100d
  removing profile version 515
  removing profile version 514
  ```

# Description

This command deletes non-current versions of a profile, making it
impossible to roll back to these versions. By default, all non-current
versions are deleted. With `--older-than` *N*`d`, all non-current
versions older than *N* days are deleted.

)""
