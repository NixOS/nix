R"(
# Examples

```console
$ nix store access info /nix/store/fzn8agjb9qmikbf97h1a5wlf3iifjgqz-foo
The path is not protected
$ cat /nix/store/fzn8agjb9qmikbf97h1a5wlf3iifjgqz-foo
foo
$ nix store access protect /nix/store/fzn8agjb9qmikbf97h1a5wlf3iifjgqz-foo
$ nix store access info /nix/store/fzn8agjb9qmikbf97h1a5wlf3iifjgqz-foo
The path is protected
No users have access to the path
$ cat /nix/store/fzn8agjb9qmikbf97h1a5wlf3iifjgqz-foo
cat: /nix/store/fzn8agjb9qmikbf97h1a5wlf3iifjgqz-foo: Permission denied
```

# Description

`nix store access protect` protects a store path from being readable and executable by arbitrary users.

You can use `nix store access grant` to grant users access to the path, and `nix store access unprotect` to remove the protection entirely.

<!-- FIXME moar docs -->

)"
