R"(
# Examples

```console
$ nix store access info /nix/store/fzn8agjb9qmikbf97h1a5wlf3iifjgqz-foo
The path is protected
The following users have access to the path:
  alice
$ nix store access info /nix/store/fzn8agjb9qmikbf97h1a5wlf3iifjgqz-foo --json
{"protected":true,users:["alice"]}
```

# Description

This command shows information about the access control list of a store path.

)"
