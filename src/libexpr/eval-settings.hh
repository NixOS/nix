#pragma once
///@file

#include "config.hh"

namespace nix {

struct EvalSettings : Config
{
    EvalSettings();

    static Strings getDefaultNixPath();

    static bool isPseudoUrl(std::string_view s);

    static std::string resolvePseudoUrl(std::string_view url);

    Setting<bool> enableNativeCode{this, false, "allow-unsafe-native-code-during-evaluation",
        "Whether builtin functions that allow executing native code should be enabled."};

    Setting<Strings> nixPath{
        this, getDefaultNixPath(), "nix-path",
        R"(
          List of search paths to use for [lookup path](@docroot@/language/constructs/lookup-path.md) resolution.
          This setting determines the value of
          [`builtins.nixPath`](@docroot@/language/builtin-constants.md#builtins-nixPath) and can be used with [`builtins.findFile`](@docroot@/language/builtin-constants.md#builtins-findFile).

          The default value is

          ```
          $HOME/.nix-defexpr/channels
          nixpkgs=$NIX_STATE_DIR/profiles/per-user/root/channels/nixpkgs
          $NIX_STATE_DIR/profiles/per-user/root/channels
          ```

          It can be overridden with the [`NIX_PATH` environment variable](@docroot@/command-ref/env-common.md#env-NIX_PATH) or the [`-I` command line option](@docroot@/command-ref/opt-common.md#opt-I).

          > **Note**
          >
          > If [pure evaluation](#conf-pure-eval) is enabled, `nixPath` evaluates to the empty list `[ ]`.
        )", {}, false};

    Setting<std::string> currentSystem{
        this, "", "eval-system",
        R"(
          This option defines
          [`builtins.currentSystem`](@docroot@/language/builtin-constants.md#builtins-currentSystem)
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
    const std::string & getCurrentSystem();

    Setting<bool> restrictEval{
        this, false, "restrict-eval",
        R"(
          If set to `true`, the Nix evaluator will not allow access to any
          files outside of
          [`builtins.nixPath`](@docroot@/language/builtin-constants.md#builtins-nixPath),
          or to URIs outside of
          [`allowed-uris`](@docroot@/command-ref/conf-file.md#conf-allowed-uris).
        )"};

    Setting<bool> pureEval{this, false, "pure-eval",
        R"(
          Pure evaluation mode ensures that the result of Nix expressions is fully determined by explicitly declared inputs, and not influenced by external state:

          - Restrict file system and network access to files specified by cryptographic hash
          - Disable impure constants:
            - [`builtins.currentSystem`](@docroot@/language/builtin-constants.md#builtins-currentSystem)
            - [`builtins.currentTime`](@docroot@/language/builtin-constants.md#builtins-currentTime)
            - [`builtins.nixPath`](@docroot@/language/builtin-constants.md#builtins-nixPath)
            - [`builtins.storePath`](@docroot@/language/builtin-constants.md#builtins-storePath)
        )"
        };

    Setting<bool> enableImportFromDerivation{
        this, true, "allow-import-from-derivation",
        R"(
          By default, Nix allows [Import from Derivation](@docroot@/language/import-from-derivation.md).

          With this option set to `false`, Nix will throw an error when evaluating an expression that uses this feature,
          even when the required store object is readily available.
          This ensures that evaluation will not require any builds to take place,
          regardless of the state of the store.
        )"};

    Setting<Strings> allowedUris{this, {}, "allowed-uris",
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

    Setting<bool> traceFunctionCalls{this, false, "trace-function-calls",
        R"(
          If set to `true`, the Nix evaluator will trace every function call.
          Nix will print a log message at the "vomit" level for every function
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

    Setting<bool> useEvalCache{this, true, "eval-cache",
        "Whether to use the flake evaluation cache."};

    Setting<bool> ignoreExceptionsDuringTry{this, false, "ignore-try",
        R"(
          If set to true, ignore exceptions inside 'tryEval' calls when evaluating nix expressions in
          debug mode (using the --debugger flag). By default the debugger will pause on all exceptions.
        )"};

    Setting<bool> traceVerbose{this, false, "trace-verbose",
        "Whether `builtins.traceVerbose` should trace its first argument when evaluated."};

    Setting<unsigned int> maxCallDepth{this, 10000, "max-call-depth",
        "The maximum function call depth to allow before erroring."};

    Setting<bool> builtinsTraceDebugger{this, false, "debugger-on-trace",
        R"(
          If set to true and the `--debugger` flag is given,
          [`builtins.trace`](@docroot@/language/builtins.md#builtins-trace) will
          enter the debugger like
          [`builtins.break`](@docroot@/language/builtins.md#builtins-break).

          This is useful for debugging warnings in third-party Nix code.
        )"};
};

extern EvalSettings evalSettings;

/**
 * Conventionally part of the default nix path in impure mode.
 */
Path getNixDefExpr();

}
