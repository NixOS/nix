R""(

# Description

Copy the file or directory *path* to the Nix store, and
print the resulting store path on standard output.

> **Warning**
>
> The resulting store path is not registered as a garbage
> collector root, so it could be deleted before you have a
> chance to register it.

# Examples

Add a regular file to the store:

```console
# echo foo > bar

# nix add-to-store --flat ./bar
/nix/store/cbv2s4bsvzjri77s2gb8g8bpcb6dpa8w-bar

# cat /nix/store/cbv2s4bsvzjri77s2gb8g8bpcb6dpa8w-bar
foo
```

)""
