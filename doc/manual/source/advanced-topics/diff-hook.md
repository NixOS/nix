# Verifying Build Reproducibility

You can use Nix's `diff-hook` setting to compare build results. Note
that this hook is only executed if the results differ; it is not used
for determining if the results are the same.

For purposes of demonstration, we'll use the following Nix file,
`deterministic.nix` for testing:

```nix
let
  inherit (import <nixpkgs> {}) runCommand;
in {
  stable = runCommand "stable" {} ''
    touch $out
  '';

  unstable = runCommand "unstable" {} ''
    echo $RANDOM > $out
  '';
}
```

Additionally, `nix.conf` contains:

    diff-hook = /etc/nix/my-diff-hook
    run-diff-hook = true

where `/etc/nix/my-diff-hook` is an executable file containing:

```bash
#!/bin/sh
exec >&2
echo "For derivation $3:"
/run/current-system/sw/bin/diff -r "$1" "$2"
```

The diff hook is executed by the same user and group who ran the build.
However, the diff hook does not have write access to the store path just
built.

# Spot-Checking Build Determinism

Verify a path which already exists in the Nix store by passing `--check`
to the build command.

If the build passes and is deterministic, Nix will exit with a status
code of 0:

```console
$ nix-build ./deterministic.nix --attr stable
this derivation will be built:
  /nix/store/z98fasz2jqy9gs0xbvdj939p27jwda38-stable.drv
building '/nix/store/z98fasz2jqy9gs0xbvdj939p27jwda38-stable.drv'...
/nix/store/yyxlzw3vqaas7wfp04g0b1xg51f2czgq-stable

$ nix-build ./deterministic.nix --attr stable --check
checking outputs of '/nix/store/z98fasz2jqy9gs0xbvdj939p27jwda38-stable.drv'...
/nix/store/yyxlzw3vqaas7wfp04g0b1xg51f2czgq-stable
```

If the build is not deterministic, Nix will exit with a status code of
1:

```console
$ nix-build ./deterministic.nix --attr unstable
this derivation will be built:
  /nix/store/cgl13lbj1w368r5z8gywipl1ifli7dhk-unstable.drv
building '/nix/store/cgl13lbj1w368r5z8gywipl1ifli7dhk-unstable.drv'...
/nix/store/krpqk0l9ib0ibi1d2w52z293zw455cap-unstable

$ nix-build ./deterministic.nix --attr unstable --check
checking outputs of '/nix/store/cgl13lbj1w368r5z8gywipl1ifli7dhk-unstable.drv'...
error: derivation '/nix/store/cgl13lbj1w368r5z8gywipl1ifli7dhk-unstable.drv' may
not be deterministic: output '/nix/store/krpqk0l9ib0ibi1d2w52z293zw455cap-unstable' differs
```

In the Nix daemon's log, we will now see:

```
For derivation /nix/store/cgl13lbj1w368r5z8gywipl1ifli7dhk-unstable.drv:
1c1
< 8108
---
> 30204
```

Using `--check` with `--keep-failed` will cause Nix to keep the second
build's output in a special, `.check` path:

```console
$ nix-build ./deterministic.nix --attr unstable --check --keep-failed
checking outputs of '/nix/store/cgl13lbj1w368r5z8gywipl1ifli7dhk-unstable.drv'...
note: keeping build directory '/tmp/nix-build-unstable.drv-0'
error: derivation '/nix/store/cgl13lbj1w368r5z8gywipl1ifli7dhk-unstable.drv' may
not be deterministic: output '/nix/store/krpqk0l9ib0ibi1d2w52z293zw455cap-unstable' differs
from '/nix/store/krpqk0l9ib0ibi1d2w52z293zw455cap-unstable.check'
```

In particular, notice the
`/nix/store/krpqk0l9ib0ibi1d2w52z293zw455cap-unstable.check` output. Nix
has copied the build results to that directory where you can examine it.

> []{#check-dirs-are-unregistered} **Note**
> 
> Check paths are not protected against garbage collection, and this
> path will be deleted on the next garbage collection.
> 
> The path is guaranteed to be alive for the duration of
> the `diff-hook`'s execution, but may be deleted any time after.
> 
> If the comparison is performed as part of automated tooling, please
> use the diff-hook or author your tooling to handle the case where the
> build was not deterministic and also a check path does not exist.

`--check` is only usable if the derivation has been built on the system
already. If the derivation has not been built Nix will fail with the
error:

    error: some outputs of '/nix/store/hzi1h60z2qf0nb85iwnpvrai3j2w7rr6-unstable.drv' 
    are not valid, so checking is not possible

Run the build without `--check`, and then try with `--check` again.
