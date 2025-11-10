#pragma once
///@file

#include "nix/expr/eval-profiler-settings.hh"
#include "nix/util/configuration.hh"
#include "nix/util/source-path.hh"

namespace nix {

class EvalState;
struct PrimOp;

struct EvalSettings : Config
{
    /**
     * Function used to interpret look path entries of a given scheme.
     *
     * The argument is the non-scheme part of the lookup path entry (see
     * `LookupPathHooks` below).
     *
     * The return value is (a) whether the entry was valid, and, if so,
     * what does it map to.
     */
    using LookupPathHook = std::optional<SourcePath>(EvalState & state, std::string_view);

    /**
     * Map from "scheme" to a `LookupPathHook`.
     *
     * Given a lookup path value (i.e. either the whole thing, or after
     * the `<key>=`) in the form of:
     *
     * ```
     * <scheme>:<arbitrary string>
     * ```
     *
     * if `<scheme>` is a key in this map, then `<arbitrary string>` is
     * passed to the hook that is the value in this map.
     */
    using LookupPathHooks = std::map<std::string, std::function<LookupPathHook>>;

    EvalSettings(bool & readOnlyMode, LookupPathHooks lookupPathHooks = {});

    bool & readOnlyMode;

    static Strings getDefaultNixPath();

    static bool isPseudoUrl(std::string_view s);

    static Strings parseNixPath(const std::string & s);

    static std::string resolvePseudoUrl(std::string_view url);

    LookupPathHooks lookupPathHooks;

    std::vector<PrimOp> extraPrimOps;

    Setting<bool> enableNativeCode{this, false, "allow-unsafe-native-code-during-evaluation", R"(
        Enable built-in functions that allow executing native code.

        In particular, this adds:
        - `builtins.importNative` *path* *symbol*

          Opens dynamic shared object (DSO) at *path*, loads the function with the symbol name *symbol* from it and runs it.
          The loaded function must have the following signature:
          ```cpp
          extern "C" typedef void (*ValueInitialiser) (EvalState & state, Value & v);
          ```

          The [Nix C++ API documentation](@docroot@/development/documentation.md#api-documentation) has more details on evaluator internals.

        - `builtins.exec` *arguments*

          Execute a program, where *arguments* are specified as a list of strings, and parse its output as a Nix expression.
    )"};

    Setting<Strings> nixPath{
        this,
        {},
        "nix-path",
        R"(
          List of search paths to use for [lookup path](@docroot@/language/constructs/lookup-path.md) resolution.
          This setting determines the value of
          [`builtins.nixPath`](@docroot@/language/builtins.md#builtins-nixPath) and can be used with [`builtins.findFile`](@docroot@/language/builtins.md#builtins-findFile).

          - The configuration setting is overridden by the [`NIX_PATH`](@docroot@/command-ref/env-common.md#env-NIX_PATH)
          environment variable.
          - `NIX_PATH` is overridden by [specifying the setting as the command line flag](@docroot@/command-ref/conf-file.md#command-line-flags) `--nix-path`.
          - Any current value is extended by the [`-I` option](@docroot@/command-ref/opt-common.md#opt-I) or `--extra-nix-path`.

          If the respective paths are accessible, the default values are:

          - `$HOME/.nix-defexpr/channels`

            The [user channel link](@docroot@/command-ref/files/default-nix-expression.md#user-channel-link), pointing to the current state of [channels](@docroot@/command-ref/files/channels.md) for the current user.

          - `nixpkgs=$NIX_STATE_DIR/profiles/per-user/root/channels/nixpkgs`

            The current state of the `nixpkgs` channel for the `root` user.

          - `$NIX_STATE_DIR/profiles/per-user/root/channels`

            The current state of all channels for the `root` user.

          These files are set up by the [Nix installer](@docroot@/installation/installing-binary.md).
          See [`NIX_STATE_DIR`](@docroot@/command-ref/env-common.md#env-NIX_STATE_DIR) for details on the environment variable.

          > **Note**
          >
          > If [restricted evaluation](@docroot@/command-ref/conf-file.md#conf-restrict-eval) is enabled, the default value is empty.
          >
          > If [pure evaluation](#conf-pure-eval) is enabled, `builtins.nixPath` *always* evaluates to the empty list `[ ]`.
        )",
        {},
        false};

    Setting<std::string> currentSystem{
        this,
        "",
        "eval-system",
        R"(
          This option defines
          [`builtins.currentSystem`](@docroot@/language/builtins.md#builtins-currentSystem)
          in the Nix language if it is set as a non-empty string.
          Otherwise, if it is defined as the empty string (the default), the value of the
          [`system` ](#conf-system)
          configuration setting is used instead.

          Unlike `system`, this setting does not change what kind of derivations can be built locally.
          This is useful for evaluating Nix code on one system to produce derivations to be built on another type of system.
        )"};

    /**
     * Implements the `eval-system` vs `system` defaulting logic
     * described for `eval-system`.
     */
    const std::string & getCurrentSystem() const;

    Setting<bool> restrictEval{
        this,
        false,
        "restrict-eval",
        R"(
          If set to `true`, the Nix evaluator doesn't allow access to any
          files outside of
          [`builtins.nixPath`](@docroot@/language/builtins.md#builtins-nixPath),
          or to URIs outside of
          [`allowed-uris`](@docroot@/command-ref/conf-file.md#conf-allowed-uris).
        )"};

    Setting<bool> pureEval{
        this,
        false,
        "pure-eval",
        R"(
          Pure evaluation mode ensures that the result of Nix expressions is fully determined by explicitly declared inputs, and not influenced by external state:

          - Restrict file system and network access to files specified by cryptographic hash
          - Disable impure constants:
            - [`builtins.currentSystem`](@docroot@/language/builtins.md#builtins-currentSystem)
            - [`builtins.currentTime`](@docroot@/language/builtins.md#builtins-currentTime)
            - [`builtins.nixPath`](@docroot@/language/builtins.md#builtins-nixPath)
            - [`builtins.storePath`](@docroot@/language/builtins.md#builtins-storePath)
        )"};

    Setting<bool> traceImportFromDerivation{
        this,
        false,
        "trace-import-from-derivation",
        R"(
          By default, Nix allows [Import from Derivation](@docroot@/language/import-from-derivation.md).

          When this setting is `true`, Nix logs a warning indicating that it performed such an import.
          This option has no effect if `allow-import-from-derivation` is disabled.
        )"};

    Setting<bool> enableImportFromDerivation{
        this,
        true,
        "allow-import-from-derivation",
        R"(
          By default, Nix allows [Import from Derivation](@docroot@/language/import-from-derivation.md).

          With this option set to `false`, Nix throws an error when evaluating an expression that uses this feature,
          even when the required store object is readily available.
          This ensures that evaluation doesn't require any builds to take place,
          regardless of the state of the store.
        )"};

    Setting<Strings> allowedUris{
        this,
        {},
        "allowed-uris",
        R"(
          A list of URI prefixes to which access is allowed in restricted
          evaluation mode. For example, when set to
          `https://github.com/NixOS`, builtin functions such as `fetchGit` are
          allowed to access `https://github.com/NixOS/patchelf.git`.

          Access is granted when
          - the URI is equal to the prefix,
          - or the URI is a subpath of the prefix,
          - or the prefix is a URI scheme ended by a colon `:` and the URI has the same scheme.
        )"};

    Setting<bool> traceFunctionCalls{
        this,
        false,
        "trace-function-calls",
        R"(
          If set to `true`, the Nix evaluator traces every function call.
          Nix prints a log message at the "vomit" level for every function
          entrance and function exit.

              function-trace entered undefined position at 1565795816999559622
              function-trace exited undefined position at 1565795816999581277
              function-trace entered /nix/store/.../example.nix:226:41 at 1565795253249935150
              function-trace exited /nix/store/.../example.nix:226:41 at 1565795253249941684

          The `undefined position` means the function call is a builtin.

          Use the `contrib/stack-collapse.py` script distributed with the Nix
          source code to convert the trace logs in to a format suitable for
          `flamegraph.pl`.
        )"};

    Setting<EvalProfilerMode> evalProfilerMode{
        this,
        EvalProfilerMode::disabled,
        "eval-profiler",
        R"(
          Enables evaluation profiling. The following modes are supported:

          * `flamegraph` stack sampling profiler. Outputs folded format, one line per stack (suitable for `flamegraph.pl` and compatible tools).

          Use [`eval-profile-file`](#conf-eval-profile-file) to specify where the profile is saved.

          See [Using the `eval-profiler`](@docroot@/advanced-topics/eval-profiler.md).
        )"};

    Setting<Path> evalProfileFile{
        this,
        "nix.profile",
        "eval-profile-file",
        R"(
          Specifies the file where [evaluation profile](#conf-eval-profiler) is saved.
        )"};

    Setting<uint32_t> evalProfilerFrequency{
        this,
        99,
        "eval-profiler-frequency",
        R"(
          Specifies the sampling rate in hertz for sampling evaluation profilers.
          Use `0` to sample the stack after each function call.
          See [`eval-profiler`](#conf-eval-profiler).
        )"};

    Setting<bool> useEvalCache{
        this,
        true,
        "eval-cache",
        R"(
            Whether to use the flake evaluation cache.
            Certain commands won't have to evaluate when invoked for the second time with a particular version of a flake.
            Intermediate results are not cached.
        )"};

    Setting<bool> ignoreExceptionsDuringTry{
        this,
        false,
        "ignore-try",
        R"(
          If set to true, ignore exceptions inside 'tryEval' calls when evaluating Nix expressions in
          debug mode (using the --debugger flag). By default the debugger pauses on all exceptions.
        )"};

    Setting<bool> traceVerbose{
        this,
        false,
        "trace-verbose",
        "Whether `builtins.traceVerbose` should trace its first argument when evaluated."};

    Setting<unsigned int> maxCallDepth{
        this, 10000, "max-call-depth", "The maximum function call depth to allow before erroring."};

    Setting<bool> builtinsTraceDebugger{
        this,
        false,
        "debugger-on-trace",
        R"(
          If set to true and the `--debugger` flag is given, the following functions
          enter the debugger like [`builtins.break`](@docroot@/language/builtins.md#builtins-break):

          * [`builtins.trace`](@docroot@/language/builtins.md#builtins-trace)
          * [`builtins.traceVerbose`](@docroot@/language/builtins.md#builtins-traceVerbose)
            if [`trace-verbose`](#conf-trace-verbose) is set to true.
          * [`builtins.warn`](@docroot@/language/builtins.md#builtins-warn)

          This is useful for debugging warnings in third-party Nix code.
        )"};

    Setting<bool> builtinsDebuggerOnWarn{
        this,
        false,
        "debugger-on-warn",
        R"(
          If set to true and the `--debugger` flag is given, [`builtins.warn`](@docroot@/language/builtins.md#builtins-warn)
          will enter the debugger like [`builtins.break`](@docroot@/language/builtins.md#builtins-break).

          This is useful for debugging warnings in third-party Nix code.

          Use [`debugger-on-trace`](#conf-debugger-on-trace) to also enter the debugger on legacy warnings that are logged with [`builtins.trace`](@docroot@/language/builtins.md#builtins-trace).
        )"};

    Setting<bool> builtinsAbortOnWarn{
        this,
        false,
        "abort-on-warn",
        R"(
          If set to true, [`builtins.warn`](@docroot@/language/builtins.md#builtins-warn) throws an error when logging a warning.

          This will give you a stack trace that leads to the location of the warning.

          This is useful for finding information about warnings in third-party Nix code when you can not start the interactive debugger, such as when Nix is called from a non-interactive script. See [`debugger-on-warn`](#conf-debugger-on-warn).

          Currently, a stack trace can only be produced when the debugger is enabled, or when evaluation is aborted.

          This option can be enabled by setting `NIX_ABORT_ON_WARN=1` in the environment.
        )"};

    Setting<bool> warnShortPathLiterals{
        this,
        false,
        "warn-short-path-literals",
        R"(
          If set to true, the Nix evaluator will warn when encountering relative path literals
          that don't start with `./` or `../`.

          For example, with this setting enabled, `foo/bar` would emit a warning
          suggesting to use `./foo/bar` instead.

          This is useful for improving code readability and making path literals
          more explicit.
    )"};

    Setting<unsigned> bindingsUpdateLayerRhsSizeThreshold{
        this,
        sizeof(void *) == 4 ? 8192 : 16,
        "eval-attrset-update-layer-rhs-threshold",
        R"(
          Tunes the maximum size of an attribute set that, when used
          as a right operand in an [attribute set update expression](@docroot@/language/operators.md#update),
          uses a more space-efficient linked-list representation of attribute sets.

          Setting this to larger values generally leads to less memory allocations,
          but may lead to worse evaluation performance.

          A value of `0` disables this optimization completely.

          This is an advanced performance tuning option and typically should not be changed.
          The default value is chosen to balance performance and memory usage. On 32 bit systems
          where memory is scarce, the default is a large value to reduce the amount of allocations.
    )"};
};

/**
 * Conventionally part of the default nix path in impure mode.
 */
Path getNixDefExpr();

} // namespace nix
