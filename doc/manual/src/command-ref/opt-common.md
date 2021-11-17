# Common Options

Most Nix commands accept the following command-line options:

  - `--help`\
    Prints out a summary of the command syntax and exits.

  - `--version`\
    Prints out the Nix version number on standard output and exits.

  - `--verbose` / `-v`\
    Increases the level of verbosity of diagnostic messages printed on
    standard error. For each Nix operation, the information printed on
    standard output is well-defined; any diagnostic information is
    printed on standard error, never on standard output.

    This option may be specified repeatedly. Currently, the following
    verbosity levels exist:

      - 0\
        “Errors only”: only print messages explaining why the Nix
        invocation failed.

      - 1\
        “Informational”: print *useful* messages about what Nix is
        doing. This is the default.

      - 2\
        “Talkative”: print more informational messages.

      - 3\
        “Chatty”: print even more informational messages.

      - 4\
        “Debug”: print debug information.

      - 5\
        “Vomit”: print vast amounts of debug information.

  - `--quiet`\
    Decreases the level of verbosity of diagnostic messages printed on
    standard error. This is the inverse option to `-v` / `--verbose`.

    This option may be specified repeatedly. See the previous verbosity
    levels list.

  - `--log-format` *format*\
    This option can be used to change the output of the log format, with
    *format* being one of:

      - raw\
        This is the raw format, as outputted by nix-build.

      - internal-json\
        Outputs the logs in a structured manner.

        > **Warning**
        >
        > While the schema itself is relatively stable, the format of
        > the error-messages (namely of the `msg`-field) can change
        > between releases.

      - bar\
        Only display a progress bar during the builds.

      - bar-with-logs\
        Display the raw logs, with the progress bar at the bottom.

  - `--no-build-output` / `-Q`\
    By default, output written by builders to standard output and
    standard error is echoed to the Nix command's standard error. This
    option suppresses this behaviour. Note that the builder's standard
    output and error are always written to a log file in
    `prefix/nix/var/log/nix`.

  - `--max-jobs` / `-j` *number*\
    Sets the maximum number of build jobs that Nix will perform in
    parallel to the specified number. Specify `auto` to use the number
    of CPUs in the system. The default is specified by the `max-jobs`
    configuration setting, which itself defaults to `1`. A higher
    value is useful on SMP systems or to exploit I/O latency.

    Setting it to `0` disallows building on the local machine, which is
    useful when you want builds to happen only on remote builders.

  - `--cores`\
    Sets the value of the `NIX_BUILD_CORES` environment variable in
    the invocation of builders. Builders can use this variable at
    their discretion to control the maximum amount of parallelism. For
    instance, in Nixpkgs, if the derivation attribute
    `enableParallelBuilding` is set to `true`, the builder passes the
    `-jN` flag to GNU Make. It defaults to the value of the `cores`
    configuration setting, if set, or `1` otherwise. The value `0`
    means that the builder should use all available CPU cores in the
    system.

  - `--max-silent-time`\
    Sets the maximum number of seconds that a builder can go without
    producing any data on standard output or standard error. The
    default is specified by the `max-silent-time` configuration
    setting. `0` means no time-out.

  - `--timeout`\
    Sets the maximum number of seconds that a builder can run. The
    default is specified by the `timeout` configuration setting. `0`
    means no timeout.

  - `--keep-going` / `-k`\
    Keep going in case of failed builds, to the greatest extent
    possible. That is, if building an input of some derivation fails,
    Nix will still build the other inputs, but not the derivation
    itself. Without this option, Nix stops if any build fails (except
    for builds of substitutes), possibly killing builds in progress (in
    case of parallel or distributed builds).

  - `--keep-failed` / `-K`\
    Specifies that in case of a build failure, the temporary directory
    (usually in `/tmp`) in which the build takes place should not be
    deleted. The path of the build directory is printed as an
    informational message.

  - `--fallback`\
    Whenever Nix attempts to build a derivation for which substitutes
    are known for each output path, but realising the output paths
    through the substitutes fails, fall back on building the derivation.

    The most common scenario in which this is useful is when we have
    registered substitutes in order to perform binary distribution from,
    say, a network repository. If the repository is down, the
    realisation of the derivation will fail. When this option is
    specified, Nix will build the derivation instead. Thus, installation
    from binaries falls back on installation from source. This option is
    not the default since it is generally not desirable for a transient
    failure in obtaining the substitutes to lead to a full build from
    source (with the related consumption of resources).

  - `--readonly-mode`\
    When this option is used, no attempt is made to open the Nix
    database. Most Nix operations do need database access, so those
    operations will fail.

  - `--arg` *name* *value*\
    This option is accepted by `nix-env`, `nix-instantiate`,
    `nix-shell` and `nix-build`. When evaluating Nix expressions, the
    expression evaluator will automatically try to call functions that
    it encounters. It can automatically call functions for which every
    argument has a [default
    value](../expressions/language-constructs.md#functions) (e.g.,
    `{ argName ?  defaultValue }: ...`). With `--arg`, you can also
    call functions that have arguments without a default value (or
    override a default value). That is, if the evaluator encounters a
    function with an argument named *name*, it will call it with value
    *value*.

    For instance, the top-level `default.nix` in Nixpkgs is actually a
    function:

    ```nix
    { # The system (e.g., `i686-linux') for which to build the packages.
      system ? builtins.currentSystem
      ...
    }: ...
    ```

    So if you call this Nix expression (e.g., when you do `nix-env -iA
    pkgname`), the function will be called automatically using the
    value [`builtins.currentSystem`](../expressions/builtins.md) for
    the `system` argument. You can override this using `--arg`, e.g.,
    `nix-env -iA pkgname --arg system \"i686-freebsd\"`. (Note that
    since the argument is a Nix string literal, you have to escape the
    quotes.)

  - `--argstr` *name* *value*\
    This option is like `--arg`, only the value is not a Nix
    expression but a string. So instead of `--arg system
    \"i686-linux\"` (the outer quotes are to keep the shell happy) you
    can say `--argstr system i686-linux`.

  - `--attr` / `-A` *attrPath*\
    Select an attribute from the top-level Nix expression being
    evaluated. (`nix-env`, `nix-instantiate`, `nix-build` and
    `nix-shell` only.) The *attribute path* *attrPath* is a sequence
    of attribute names separated by dots. For instance, given a
    top-level Nix expression *e*, the attribute path `xorg.xorgserver`
    would cause the expression `e.xorg.xorgserver` to be used. See
    [`nix-env --install`](nix-env.md#operation---install) for some
    concrete examples.

    In addition to attribute names, you can also specify array indices.
    For instance, the attribute path `foo.3.bar` selects the `bar`
    attribute of the fourth element of the array in the `foo` attribute
    of the top-level expression.

  - `--expr` / `-E`\
    Interpret the command line arguments as a list of Nix expressions to
    be parsed and evaluated, rather than as a list of file names of Nix
    expressions. (`nix-instantiate`, `nix-build` and `nix-shell` only.)

    For `nix-shell`, this option is commonly used to give you a shell in
    which you can build the packages returned by the expression. If you
    want to get a shell which contain the *built* packages ready for
    use, give your expression to the `nix-shell -p` convenience flag
    instead.

  - `-I` *path*\
    Add a path to the Nix expression search path. This option may be
    given multiple times. See the `NIX_PATH` environment variable for
    information on the semantics of the Nix search path. Paths added
    through `-I` take precedence over `NIX_PATH`.

  - `--option` *name* *value*\
    Set the Nix configuration option *name* to *value*. This overrides
    settings in the Nix configuration file (see nix.conf5).

  - `--repair`\
    Fix corrupted or missing store paths by redownloading or rebuilding
    them. Note that this is slow because it requires computing a
    cryptographic hash of the contents of every path in the closure of
    the build. Also note the warning under `nix-store --repair-path`.
