#ifdef __APPLE__

#  include <spawn.h>
#  include <sys/sysctl.h>
#  include <sandbox.h>

/* This definition is undocumented but depended upon by all major browsers. */
extern "C" int
sandbox_init_with_parameters(const char * profile, uint64_t flags, const char * const parameters[], char ** errorbuf);

namespace nix {

struct DarwinDerivationBuilder : DerivationBuilderImpl
{
    PathsInChroot pathsInChroot;

    /**
     * Whether full sandboxing is enabled. Note that macOS builds
     * always have *some* sandboxing (see sandbox-minimal.sb).
     */
    bool useSandbox;

    DarwinDerivationBuilder(
        LocalStore & store,
        std::unique_ptr<DerivationBuilderCallbacks> miscMethods,
        DerivationBuilderParams params,
        bool useSandbox)
        : DerivationBuilderImpl(store, std::move(miscMethods), std::move(params))
        , useSandbox(useSandbox)
    {
    }

    void prepareSandbox() override
    {
        pathsInChroot = getPathsInSandbox();
    }

    void setUser() override
    {
        DerivationBuilderImpl::setUser();

        /* This has to appear before import statements. */
        std::string sandboxProfile = "(version 1)\n";

        if (useSandbox) {

            /* Lots and lots and lots of file functions freak out if they can't stat their full ancestry */
            PathSet ancestry;

            /* We build the ancestry before adding all inputPaths to the store because we know they'll
               all have the same parents (the store), and there might be lots of inputs. This isn't
               particularly efficient... I doubt it'll be a bottleneck in practice */
            for (auto & i : pathsInChroot) {
                Path cur = i.first;
                while (cur.compare("/") != 0) {
                    cur = dirOf(cur);
                    ancestry.insert(cur);
                }
            }

            /* And we want the store in there regardless of how empty pathsInChroot. We include the innermost
               path component this time, since it's typically /nix/store and we care about that. */
            Path cur = store.storeDir;
            while (cur.compare("/") != 0) {
                ancestry.insert(cur);
                cur = dirOf(cur);
            }

            /* Add all our input paths to the chroot */
            for (auto & i : inputPaths) {
                auto p = store.printStorePath(i);
                pathsInChroot.insert_or_assign(p, ChrootPath{.source = p});
            }

            /* Violations will go to the syslog if you set this. Unfortunately the destination does not appear to be
             * configurable */
            if (settings.darwinLogSandboxViolations) {
                sandboxProfile += "(deny default)\n";
            } else {
                sandboxProfile += "(deny default (with no-log))\n";
            }

            sandboxProfile +=
#  include "sandbox-defaults.sb"
                ;

            if (!derivationType.isSandboxed())
                sandboxProfile +=
#  include "sandbox-network.sb"
                    ;

            /* Add the output paths we'll use at build-time to the chroot */
            sandboxProfile += "(allow file-read* file-write* process-exec\n";
            for (auto & [_, path] : scratchOutputs)
                sandboxProfile += fmt("\t(subpath \"%s\")\n", store.printStorePath(path));

            sandboxProfile += ")\n";

            /* Our inputs (transitive dependencies and any impurities computed above)

               without file-write* allowed, access() incorrectly returns EPERM
             */
            sandboxProfile += "(allow file-read* file-write* process-exec\n";

            // We create multiple allow lists, to avoid exceeding a limit in the darwin sandbox interpreter.
            // See https://github.com/NixOS/nix/issues/4119
            // We split our allow groups approximately at half the actual limit, 1 << 16
            const size_t breakpoint = sandboxProfile.length() + (1 << 14);
            for (auto & i : pathsInChroot) {

                if (sandboxProfile.length() >= breakpoint) {
                    debug("Sandbox break: %d %d", sandboxProfile.length(), breakpoint);
                    sandboxProfile += ")\n(allow file-read* file-write* process-exec\n";
                }

                if (i.first != i.second.source)
                    throw Error(
                        "can't map '%1%' to '%2%': mismatched impure paths not supported on Darwin",
                        i.first,
                        i.second.source);

                std::string path = i.first;
                auto optSt = maybeLstat(path.c_str());
                if (!optSt) {
                    if (i.second.optional)
                        continue;
                    throw SysError("getting attributes of required path '%s", path);
                }
                if (S_ISDIR(optSt->st_mode))
                    sandboxProfile += fmt("\t(subpath \"%s\")\n", path);
                else
                    sandboxProfile += fmt("\t(literal \"%s\")\n", path);
            }
            sandboxProfile += ")\n";

            /* Allow file-read* on full directory hierarchy to self. Allows realpath() */
            sandboxProfile += "(allow file-read*\n";
            for (auto & i : ancestry) {
                sandboxProfile += fmt("\t(literal \"%s\")\n", i);
            }
            sandboxProfile += ")\n";

            sandboxProfile += drvOptions.additionalSandboxProfile;
        } else
            sandboxProfile +=
#  include "sandbox-minimal.sb"
                ;

        debug("Generated sandbox profile:");
        debug(sandboxProfile);

        /* The tmpDir in scope points at the temporary build directory for our derivation. Some packages try different
           mechanisms to find temporary directories, so we want to open up a broader place for them to put their files,
           if needed. */
        Path globalTmpDir = canonPath(defaultTempDir(), true);

        /* They don't like trailing slashes on subpath directives */
        while (!globalTmpDir.empty() && globalTmpDir.back() == '/')
            globalTmpDir.pop_back();

        if (getEnv("_NIX_TEST_NO_SANDBOX") != "1") {
            Strings sandboxArgs;
            sandboxArgs.push_back("_NIX_BUILD_TOP");
            sandboxArgs.push_back(tmpDir);
            sandboxArgs.push_back("_GLOBAL_TMP_DIR");
            sandboxArgs.push_back(globalTmpDir);
            if (drvOptions.allowLocalNetworking) {
                sandboxArgs.push_back("_ALLOW_LOCAL_NETWORKING");
                sandboxArgs.push_back("1");
            }
            char * sandbox_errbuf = nullptr;
            if (sandbox_init_with_parameters(
                    sandboxProfile.c_str(), 0, stringsToCharPtrs(sandboxArgs).data(), &sandbox_errbuf)) {
                writeFull(
                    STDERR_FILENO,
                    fmt("failed to configure sandbox: %s\n", sandbox_errbuf ? sandbox_errbuf : "(null)"));
                _exit(1);
            }
        }
    }

    void execBuilder(const Strings & args, const Strings & envStrs) override
    {
        posix_spawnattr_t attrp;

        if (posix_spawnattr_init(&attrp))
            throw SysError("failed to initialize builder");

        if (posix_spawnattr_setflags(&attrp, POSIX_SPAWN_SETEXEC))
            throw SysError("failed to initialize builder");

        if (drv.platform == "aarch64-darwin") {
            // Unset kern.curproc_arch_affinity so we can escape Rosetta
            int affinity = 0;
            sysctlbyname("kern.curproc_arch_affinity", NULL, NULL, &affinity, sizeof(affinity));

            cpu_type_t cpu = CPU_TYPE_ARM64;
            posix_spawnattr_setbinpref_np(&attrp, 1, &cpu, NULL);
        } else if (drv.platform == "x86_64-darwin") {
            cpu_type_t cpu = CPU_TYPE_X86_64;
            posix_spawnattr_setbinpref_np(&attrp, 1, &cpu, NULL);
        }

        posix_spawn(
            NULL, drv.builder.c_str(), NULL, &attrp, stringsToCharPtrs(args).data(), stringsToCharPtrs(envStrs).data());
    }
};

} // namespace nix

#endif
