# Building and Testing

You can now try to build Hello. Of course, you could do `nix-env -i
hello`, but you may not want to install a possibly broken package just
yet. The best way to test the package is by using the command
`nix-build`, which builds a Nix expression and creates a symlink named
`result` in the current directory:

```console
$ nix-build -A hello
building path `/nix/store/632d2b22514d...-hello-2.1.1'
hello-2.1.1/
hello-2.1.1/intl/
hello-2.1.1/intl/ChangeLog
...

$ ls -l result
lrwxrwxrwx ... 2006-09-29 10:43 result -> /nix/store/632d2b22514d...-hello-2.1.1

$ ./result/bin/hello
Hello, world!
```

The `-A` option selects the `hello` attribute. This is faster than
using the symbolic package name specified by the `name` attribute
(which also happens to be `hello`) and is unambiguous (there can be
multiple packages with the symbolic name `hello`, but there can be
only one attribute in a set named `hello`).

`nix-build` registers the `./result` symlink as a garbage collection
root, so unless and until you delete the `./result` symlink, the output
of the build will be safely kept on your system. You can use
`nix-build`’s `-o` switch to give the symlink another name.

Nix has transactional semantics. Once a build finishes successfully, Nix
makes a note of this in its database: it registers that the path denoted
by `out` is now “valid”. If you try to build the derivation again, Nix
will see that the path is already valid and finish immediately. If a
build fails, either because it returns a non-zero exit code, because Nix
or the builder are killed, or because the machine crashes, then the
output paths will not be registered as valid. If you try to build the
derivation again, Nix will remove the output paths if they exist (e.g.,
because the builder died half-way through `make
install`) and try again. Note that there is no “negative caching”: Nix
doesn't remember that a build failed, and so a failed build can always
be repeated. This is because Nix cannot distinguish between permanent
failures (e.g., a compiler error due to a syntax error in the source)
and transient failures (e.g., a disk full condition).

Nix also performs locking. If you run multiple Nix builds
simultaneously, and they try to build the same derivation, the first Nix
instance that gets there will perform the build, while the others block
(or perform other derivations if available) until the build finishes:

```console
$ nix-build -A hello
waiting for lock on `/nix/store/0h5b7hp8d4hqfrw8igvx97x1xawrjnac-hello-2.1.1x'
```

So it is always safe to run multiple instances of Nix in parallel (which
isn’t the case with, say, `make`).
