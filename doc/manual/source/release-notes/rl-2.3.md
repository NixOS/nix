# Release 2.3 (2019-09-04)

This is primarily a bug fix release. However, it makes some incompatible
changes:

  - Nix now uses BSD file locks instead of POSIX file locks. Because of
    this, you should not use Nix 2.3 and previous releases at the same
    time on a Nix store.

It also has the following changes:

  - `builtins.fetchGit`'s `ref` argument now allows specifying an
    absolute remote ref. Nix will automatically prefix `ref` with
    `refs/heads` only if `ref` doesn't already begin with `refs/`.

  - The installer now enables sandboxing by default on Linux when the
    system has the necessary kernel support.

  - The `max-jobs` setting now defaults to 1.

  - New builtin functions: `builtins.isPath`, `builtins.hashFile`.

  - The `nix` command has a new `--print-build-logs` (`-L`) flag to
    print build log output to stderr, rather than showing the last log
    line in the progress bar. To distinguish between concurrent builds,
    log lines are prefixed by the name of the package.

  - Builds are now executed in a pseudo-terminal, and the `TERM`
    environment variable is set to `xterm-256color`. This allows many
    programs (e.g. `gcc`, `clang`, `cmake`) to print colorized log
    output.

  - Add `--no-net` convenience flag. This flag disables substituters;
    sets the `tarball-ttl` setting to infinity (ensuring that any
    previously downloaded files are considered current); and disables
    retrying downloads and sets the connection timeout to the minimum.
    This flag is enabled automatically if there are no configured
    non-loopback network interfaces.

  - Add a `post-build-hook` setting to run a program after a build has
    succeeded.

  - Add a `trace-function-calls` setting to log the duration of Nix
    function calls to stderr.
