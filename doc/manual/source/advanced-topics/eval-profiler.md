# Using the `eval-profiler`

Nix evaluator supports [evaluation](@docroot@/language/evaluation.md)
[profiling](<https://en.wikipedia.org/wiki/Profiling_(computer_programming)>)
compatible with `flamegraph.pl`. The profiler samples the nix
function call stack at regular intervals. It can be enabled with the
[`eval-profiler`](@docroot@/command-ref/conf-file.md#conf-eval-profiler)
setting:

```console
$ nix-instantiate "<nixpkgs>" -A hello --eval-profiler flamegraph
```

Stack sampling frequency and the output file path can be configured with
[`eval-profile-file`](@docroot@/command-ref/conf-file.md#conf-eval-profile-file)
and [`eval-profiler-frequency`](@docroot@/command-ref/conf-file.md#conf-eval-profiler-frequency).
By default the collected profile is saved to `nix.profile` file in the current working directory.

The collected profile can be directly consumed by `flamegraph.pl`:

```console
$ flamegraph.pl nix.profile > flamegraph.svg
```

The line information in the profile contains the location of the [call
site](https://en.wikipedia.org/wiki/Call_site) position and the name of the
function being called (when available). For example:

```
/nix/store/x9wnkly3k1gkq580m90jjn32q9f05q2v-source/pkgs/top-level/default.nix:167:5:primop import
```

Here `import` primop is called at `/nix/store/x9wnkly3k1gkq580m90jjn32q9f05q2v-source/pkgs/top-level/default.nix:167:5`.
