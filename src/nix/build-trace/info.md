R"MdBoundary(
# Description

Display some information about the given build trace

# Examples

Show some information about the build trace of the `hello` package:

```console
$ nix build-trace info nixpkgs#hello --json
[{"id":"sha256:3d382378a00588e064ee30be96dd0fa7e7df7cf3fbcace85a0e7b7dada1eef25!out","outPath":"fd3m7xawvrqcg98kgz5hc2vk3x9q0lh7-hello"}]
```

)MdBoundary"
