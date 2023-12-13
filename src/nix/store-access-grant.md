R"(
# Examples

```console
$ nix store access info /nix/store/fzn8agjb9qmikbf97h1a5wlf3iifjgqz-foo
The path is protected
The following users have access to the path:
  alice
$ nix store access grant /nix/store/fzn8agjb9qmikbf97h1a5wlf3iifjgqz-foo --user bob --user carol
$ nix store access info /nix/store/fzn8agjb9qmikbf97h1a5wlf3iifjgqz-foo
The path is protected
The following users have access to the path:
  alice
  bob
  carol
```

# Description

`nix store access grant` grants users access to store paths.

<!-- FIXME moar docs -->

)"
