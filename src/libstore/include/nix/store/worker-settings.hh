#pragma once
///@file

#include "nix/util/configuration.hh"
#include "nix/store/global-paths.hh"
#include "nix/store/store-reference.hh"

namespace nix {

struct MaxBuildJobsSetting : public BaseSetting<unsigned int>
{
    MaxBuildJobsSetting(
        Config * options,
        unsigned int def,
        const std::string & name,
        const std::string & description,
        const StringSet & aliases = {})
        : BaseSetting<unsigned int>(def, true, name, description, aliases)
    {
        options->addSetting(this);
    }

    unsigned int parse(const std::string & str) const override;
};

struct WorkerSettings : public virtual Config
{
protected:
    WorkerSettings() = default;

public:
    Setting<bool> keepGoing{
        this, false, "keep-going", "Whether to keep building derivations when another build fails."};

    Setting<bool> tryFallback{
        this,
        false,
        "fallback",
        R"(
          If set to `true`, Nix falls back to building from source if a
          binary substitute fails. This is equivalent to the `--fallback`
          flag. The default is `false`.
        )",
        {"build-fallback"}};

    Setting<size_t> logLines{
        this,
        25,
        "log-lines",
        "The number of lines of the tail of "
        "the log to show if a build fails."};

    MaxBuildJobsSetting maxBuildJobs{
        this,
        1,
        "max-jobs",
        R"(
          Maximum number of jobs that Nix tries to build locally in parallel.

          The special value `auto` causes Nix to use the number of CPUs in your system.
          Use `0` to disable local builds and directly use the remote machines specified in [`builders`](#conf-builders).
          This doesn't affect derivations that have [`preferLocalBuild = true`](@docroot@/language/advanced-attributes.md#adv-attr-preferLocalBuild), which are always built locally.

          > **Note**
          >
          > The number of CPU cores to use for each build job is independently determined by the [`cores`](#conf-cores) setting.

          <!-- TODO(@fricklerhandwerk): would be good to have those shorthands for common options as part of the specification -->
          The setting can be overridden using the `--max-jobs` (`-j`) command line switch.
        )",
        {"build-max-jobs"}};

    Setting<unsigned int> maxSubstitutionJobs{
        this,
        16,
        "max-substitution-jobs",
        R"(
          This option defines the maximum number of substitution jobs that Nix
          tries to run in parallel. The default is `16`. The minimum value
          one can choose is `1` and lower values are interpreted as `1`.
        )",
        {"substitution-max-jobs"}};

    Setting<time_t> maxSilentTime{
        this,
        0,
        "max-silent-time",
        R"(
          This option defines the maximum number of seconds that a builder can
          go without producing any data on standard output or standard error.
          This is useful (for instance in an automated build system) to catch
          builds that are stuck in an infinite loop, or to catch remote builds
          that are hanging due to network problems. It can be overridden using
          the `--max-silent-time` command line switch.

          The value `0` means that there is no timeout. This is also the
          default.
        )",
        {"build-max-silent-time"}};

    Setting<time_t> buildTimeout{
        this,
        0,
        "timeout",
        R"(
          This option defines the maximum number of seconds that a builder can
          run. This is useful (for instance in an automated build system) to
          catch builds that are stuck in an infinite loop but keep writing to
          their standard output or standard error. It can be overridden using
          the `--timeout` command line switch.

          The value `0` means that there is no timeout. This is also the
          default.
        )",
        {"build-timeout"}};

    Setting<Strings> buildHook{
        this,
        {"nix", "__build-remote"},
        "build-hook",
        R"(
          The path to the helper program that executes remote builds.

          Nix communicates with the build hook over `stdio` using a custom protocol to request builds that cannot be performed directly by the Nix daemon.
          The default value is the internal Nix binary that implements remote building.

          > **Important**
          >
          > Change this setting only if you really know what you’re doing.
        )"};

    Setting<std::string> builders{
        this,
        "@" + (nixConfDir() / "machines").string(),
        "builders",
        R"(
          A semicolon- or newline-separated list of build machines.

          In addition to the [usual ways of setting configuration options](@docroot@/command-ref/conf-file.md), the value can be read from a file by prefixing its absolute path with `@`.

          > **Example**
          >
          > This is the default setting:
          >
          > ```
          > builders = @/etc/nix/machines
          > ```

          Each machine specification consists of the following elements, separated by spaces.
          Only the first element is required.
          To leave a field at its default, set it to `-`.

          1. The URI of the remote store in the format `ssh://[username@]hostname[:port]`.

             > **Example**
             >
             > `ssh://nix@mac`

             For backward compatibility, `ssh://` may be omitted.
             The hostname may be an alias defined in `~/.ssh/config`.

          2. A comma-separated list of [Nix system types](@docroot@/development/building.md#system-type).
             If omitted, this defaults to the local platform type.

             > **Example**
             >
             > `aarch64-darwin`

             It is possible for a machine to support multiple platform types.

             > **Example**
             >
             > `i686-linux,x86_64-linux`

          3. The SSH identity file to be used to log in to the remote machine.
             If omitted, SSH uses its regular identities.

             > **Example**
             >
             > `/home/user/.ssh/id_mac`

          4. The maximum number of builds that Nix executes in parallel on the machine.
             Typically this should be equal to the number of CPU cores.

          5. The “speed factor”, indicating the relative speed of the machine as a positive integer.
             If there are multiple machines of the right type, Nix prefers the fastest, taking load into account.

          6. A comma-separated list of supported [system features](#conf-system-features).

             A machine is only used to build a derivation if all the features in the derivation's [`requiredSystemFeatures`](@docroot@/language/advanced-attributes.html#adv-attr-requiredSystemFeatures) attribute are supported by that machine.

          7. A comma-separated list of required [system features](#conf-system-features).

             A machine is only used to build a derivation if all of the machine’s required features appear in the derivation’s [`requiredSystemFeatures`](@docroot@/language/advanced-attributes.html#adv-attr-requiredSystemFeatures) attribute.

          8. The (base64-encoded) public host key of the remote machine.
             If omitted, SSH uses its regular `known_hosts` file.

             The value for this field can be obtained via `base64 -w0`.

          > **Example**
          >
          > Multiple builders specified on the command line:
          >
          > ```console
          > --builders 'ssh://mac x86_64-darwin ; ssh://beastie x86_64-freebsd'
          > ```

          > **Example**
          >
          > This specifies several machines that can perform `i686-linux` builds:
          >
          > ```
          > nix@scratchy.labs.cs.uu.nl i686-linux /home/nix/.ssh/id_scratchy 8 1 kvm
          > nix@itchy.labs.cs.uu.nl    i686-linux /home/nix/.ssh/id_scratchy 8 2
          > nix@poochie.labs.cs.uu.nl  i686-linux /home/nix/.ssh/id_scratchy 1 2 kvm benchmark
          > ```
          >
          > However, `poochie` only builds derivations that have the attribute
          >
          > ```nix
          > requiredSystemFeatures = [ "benchmark" ];
          > ```
          >
          > or
          >
          > ```nix
          > requiredSystemFeatures = [ "benchmark" "kvm" ];
          > ```
          >
          > `itchy` cannot do builds that require `kvm`, but `scratchy` does support such builds.
          > For regular builds, `itchy` is preferred over `scratchy` because it has a higher speed factor.

          For Nix to use substituters, the calling user must be in the [`trusted-users`](#conf-trusted-users) list.

          > **Note**
          >
          > A build machine must be accessible via SSH and have Nix installed.
          > `nix` must be available in `$PATH` for the user connecting over SSH.

          > **Warning**
          >
          > If you are building via the Nix daemon (default), the Nix daemon user account on the local machine (that is, `root`) requires access to a user account on the remote machine (not necessarily `root`).
          >
          > If you can’t or don’t want to configure `root` to be able to access the remote machine, set [`store`](#conf-store) to any [local store](@docroot@/store/types/local-store.html), e.g. by passing `--store /tmp` to the command on the local machine.

          To build only on remote machines and disable local builds, set [`max-jobs`](#conf-max-jobs) to 0.

          If you want the remote machines to use substituters, set [`builders-use-substitutes`](#conf-builders-use-substitutes) to `true`.
        )",
        {},
        false};

    Setting<bool> alwaysAllowSubstitutes{
        this,
        false,
        "always-allow-substitutes",
        R"(
          If set to `true`, Nix ignores the [`allowSubstitutes`](@docroot@/language/advanced-attributes.md) attribute in derivations and always attempt to use [available substituters](#conf-substituters).
        )"};

    Setting<bool> buildersUseSubstitutes{
        this,
        false,
        "builders-use-substitutes",
        R"(
          If set to `true`, Nix instructs [remote build machines](#conf-builders) to use their own [`substituters`](#conf-substituters) if available.

          It means that remote build hosts fetch as many dependencies as possible from their own substituters (e.g, from `cache.nixos.org`) instead of waiting for the local machine to upload them all.
          This can drastically reduce build times if the network connection between the local machine and the remote build host is slow.
        )"};

    Setting<bool> useSubstitutes{
        this,
        true,
        "substitute",
        R"(
          If set to `true` (default), Nix uses binary substitutes if
          available. This option can be disabled to force building from
          source.
        )",
        {"build-use-substitutes"}};

    Setting<std::vector<StoreReference>> substituters{
        this,
        std::vector<StoreReference>{StoreReference::parse("https://cache.nixos.org/")},
        "substituters",
        R"(
          A list of [URLs of Nix stores](@docroot@/store/types/index.md#store-url-format) to be used as substituters, separated by whitespace.
          A substituter is an additional [store](@docroot@/glossary.md#gloss-store) from which Nix can obtain [store objects](@docroot@/store/store-object.md) instead of building them.

          Substituters are tried based on their priority value, which each substituter can set independently.
          Lower value means higher priority.
          The default is `https://cache.nixos.org`, which has a priority of 40.

          At least one of the following conditions must be met for Nix to use a substituter:

          - The substituter is in the [`trusted-substituters`](#conf-trusted-substituters) list
          - The user calling Nix is in the [`trusted-users`](#conf-trusted-users) list

          In addition, each store path should be trusted as described in [`trusted-public-keys`](#conf-trusted-public-keys)
        )",
        {"binary-caches"}};

    Setting<unsigned long> maxLogSize{
        this,
        0,
        "max-build-log-size",
        R"(
          This option defines the maximum number of bytes that a builder can
          write to its stdout/stderr. If the builder exceeds this limit, it's
          killed. A value of `0` (the default) means that there is no limit.
        )",
        {"build-max-log-size"}};

    Setting<unsigned int> pollInterval{this, 5, "build-poll-interval", "How often (in seconds) to poll for locks."};

    Setting<std::string> postBuildHook{
        this,
        "",
        "post-build-hook",
        R"(
          Optional. The path to a program to execute after each build.

          This option is only settable in the global `nix.conf`, or on the
          command line by trusted users.

          When using the nix-daemon, the daemon executes the hook as `root`.
          If the nix-daemon is not involved, the hook runs as the user
          executing the nix-build.

            - The hook executes after an evaluation-time build.

            - The hook does not execute on substituted paths.

            - The hook's output always goes to the user's terminal.

            - If the hook fails, the build succeeds but no further builds
              execute.

            - The hook executes synchronously, and blocks other builds from
              progressing while it runs.

          The program executes with no arguments. The program's environment
          contains the following environment variables:

            - `DRV_PATH`
              The derivation for the built paths.

              Example:
              `/nix/store/5nihn1a7pa8b25l9zafqaqibznlvvp3f-bash-4.4-p23.drv`

            - `OUT_PATHS`
              Output paths of the built derivation, separated by a space
              character.

              Example:
              `/nix/store/l88brggg9hpy96ijds34dlq4n8fan63g-bash-4.4-p23-dev
              /nix/store/vch71bhyi5akr5zs40k8h2wqxx69j80l-bash-4.4-p23-doc
              /nix/store/c5cxjywi66iwn9dcx5yvwjkvl559ay6p-bash-4.4-p23-info
              /nix/store/scz72lskj03ihkcn42ias5mlp4i4gr1k-bash-4.4-p23-man
              /nix/store/a724znygmd1cac856j3gfsyvih3lw07j-bash-4.4-p23`.
        )"};
};

} // namespace nix
