R"(
# Examples

```console
$ nix store access info /nix/store/fzn8agjb9qmikbf97h1a5wlf3iifjgqz-foo
The path is protected
The following users have access to the path:
  alice
  bob
  carol
$ nix store access revoke /nix/store/fzn8agjb9qmikbf97h1a5wlf3iifjgqz-foo --user bob --user carol
$ nix store access info /nix/store/fzn8agjb9qmikbf97h1a5wlf3iifjgqz-foo
The path is protected
The following users have access to the path:
  alice
```

# Description

`nix store access revoke` revokes users or groups access to store paths.

<!-- FIXME moar docs -->

Note: revoking access to a user via the `--user` flag removes the user from the list of allowed users, it may still be able to access the path through the permission of its group.

)"
