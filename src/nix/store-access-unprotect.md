R"(
# Examples

```console
$ nix store access info /nix/store/fzn8agjb9qmikbf97h1a5wlf3iifjgqz-foo
The path is protected
No users have access to the path
$ cat /nix/store/fzn8agjb9qmikbf97h1a5wlf3iifjgqz-foo
cat: /nix/store/fzn8agjb9qmikbf97h1a5wlf3iifjgqz-foo: Permission denied
$ nix store access unprotect /nix/store/fzn8agjb9qmikbf97h1a5wlf3iifjgqz-foo
$ nix store access info /nix/store/fzn8agjb9qmikbf97h1a5wlf3iifjgqz-foo
The path is not protected
$ cat /nix/store/fzn8agjb9qmikbf97h1a5wlf3iifjgqz-foo
foo
```

# Description

`nix store access unprotect` removes the ACL protection from a store path.

<!-- FIXME moar docs -->

)"
