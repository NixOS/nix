R""(

# Description

Copy *path* to the Nix store, and print the resulting store path on
standard output.

> **Warning**
>
> The resulting store path is not registered as a garbage
> collector root, so it could be deleted before you have a
> chance to register it.

# Examples

Add a directory to the store:

```console
# mkdir dir
# echo foo > dir/bar

# nix store add-path ./dir
/nix/store/6pmjx56pm94n66n4qw1nff0y1crm8nqg-dir

# cat /nix/store/6pmjx56pm94n66n4qw1nff0y1crm8nqg-dir/bar
foo
```

)""
