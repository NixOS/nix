#include "sandbox.hh"

#include <spawn.h>
#if __APPLE__
#include <mach/machine.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#endif

namespace nix {

class SandboxDarwin : public Sandbox {
public:
    void prepareChroot(const Store&, LocalDerivationGoal& goal) override {};
    void enterChroot(const Store&, LocalDerivationGoal& goal) override {
        /* We don't really have any parent prep work to do (yet?)
           All work happens in the child, instead. */
    };
    virtual std::pair<std::string, Strings> getSandboxArgs(const Derivation& drv, bool useChroot, LocalDerivationGoal::DirsInChroot &dirsInChroot, const Store& store, const LocalDerivationGoal& goal) override {
        // FIXME passing LocalDerivationGoal is ugly
       /* This has to appear before import statements. */
       std::string sandboxProfile = "(version 1)\n";

       if (useChroot) {

           /* Lots and lots and lots of file functions freak out if they can't stat their full ancestry */
           PathSet ancestry;

           /* We build the ancestry before adding all inputPaths to the store because we know they'll
              all have the same parents (the store), and there might be lots of inputs. This isn't
              particularly efficient... I doubt it'll be a bottleneck in practice */
           for (auto & i : dirsInChroot) {
               Path cur = i.first;
               while (cur.compare("/") != 0) {
                   cur = dirOf(cur);
                   ancestry.insert(cur);
               }
           }

           /* And we want the store in there regardless of how empty dirsInChroot. We include the innermost
              path component this time, since it's typically /nix/store and we care about that. */
           Path cur = store.storeDir;
           while (cur.compare("/") != 0) {
               ancestry.insert(cur);
               cur = dirOf(cur);
           }

           /* Add all our input paths to the chroot */
           for (auto & i : goal.inputPaths) {
               auto p = store.printStorePath(i);
               dirsInChroot[p] = p;
           }

           /* Violations will go to the syslog if you set this. Unfortunately the destination does not appear to be configurable */
#if __APPLE__
           if (settings.darwinLogSandboxViolations) {
               sandboxProfile += "(deny default)\n";
           } else
#endif
           {
               sandboxProfile += "(deny default (with no-log))\n";
           }

           sandboxProfile +=
               #include "sandbox-defaults.sb"
               ;

           if (!drv.type().isSandboxed())
               sandboxProfile +=
                   #include "sandbox-network.sb"
                   ;

           /* Add the output paths we'll use at build-time to the chroot */
           sandboxProfile += "(allow file-read* file-write* process-exec\n";
           for (auto & [_, path] : goal.scratchOutputs)
               sandboxProfile += fmt("\t(subpath \"%s\")\n", store.printStorePath(path));

           sandboxProfile += ")\n";

           /* Our inputs (transitive dependencies and any impurities computed above)

              without file-write* allowed, access() incorrectly returns EPERM
            */
           sandboxProfile += "(allow file-read* file-write* process-exec\n";
           for (auto & i : dirsInChroot) {
               if (i.first != i.second.source)
                   throw Error(
                       "can't map '%1%' to '%2%': mismatched impure paths not supported on Darwin",
                       i.first, i.second.source);

               std::string path = i.first;
               struct stat st;
               if (lstat(path.c_str(), &st)) {
                   if (i.second.optional && errno == ENOENT)
                       continue;
                   throw SysError("getting attributes of path '%s", path);
               }
               if (S_ISDIR(st.st_mode))
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
#if __APPLE__
           sandboxProfile += goal.additionalSandboxProfile;
#endif
       } else
           sandboxProfile +=
               #include "sandbox-minimal.sb"
               ;

       debug("Generated sandbox profile:");
       debug(sandboxProfile);

       Path sandboxFile = goal.tmpDir + "/.sandbox.sb";

       writeFile(sandboxFile, sandboxProfile);

       bool allowLocalNetworking = goal.parsedDrv->getBoolAttr("__darwinAllowLocalNetworking");

       /* The tmpDir in scope points at the temporary build directory for our derivation. Some packages try different mechanisms
          to find temporary directories, so we want to open up a broader place for them to dump their files, if needed. */
       Path globalTmpDir = canonPath(getEnv("TMPDIR").value_or("/tmp"), true);

       /* They don't like trailing slashes on subpath directives */
       if (globalTmpDir.back() == '/') globalTmpDir.pop_back();

       if (getEnv("_NIX_TEST_NO_SANDBOX") != "1") {
           auto builder = "/usr/bin/sandbox-exec";
           Strings args;
           args.push_back("sandbox-exec");
           args.push_back("-f");
           args.push_back(sandboxFile);
           args.push_back("-D");
           args.push_back("_GLOBAL_TMP_DIR=" + globalTmpDir);
           if (allowLocalNetworking) {
               args.push_back("-D");
               args.push_back(std::string("_ALLOW_LOCAL_NETWORKING=1"));
           }
           args.push_back(drv.builder);
           return {builder, args};
       } else {
           return Sandbox::getSandboxArgs(drv, useChroot, dirsInChroot, store, goal);
       }
    };

    void spawn(const std::string& builder, const Strings& args, const Strings& envStrs, std::string_view platform) override {
        posix_spawnattr_t attrp;

        if (posix_spawnattr_init(&attrp))
            throw SysError("failed to initialize builder");

#if __APPLE__
        if (posix_spawnattr_setflags(&attrp, POSIX_SPAWN_SETEXEC))
            throw SysError("failed to initialize builder");

        if (platform == "aarch64-darwin") {
            // Unset kern.curproc_arch_affinity so we can escape Rosetta
            int affinity = 0;
            sysctlbyname("kern.curproc_arch_affinity", NULL, NULL, &affinity, sizeof(affinity));

            cpu_type_t cpu = CPU_TYPE_ARM64;
            posix_spawnattr_setbinpref_np(&attrp, 1, &cpu, NULL);
        } else if (platform == "x86_64-darwin") {
            cpu_type_t cpu = CPU_TYPE_X86_64;
            posix_spawnattr_setbinpref_np(&attrp, 1, &cpu, NULL);
        }

#endif
        posix_spawn(NULL, builder.c_str(), NULL, &attrp, stringsToCharPtrs(args).data(), stringsToCharPtrs(envStrs).data());
    };
    virtual ~SandboxDarwin() {};
};

std::unique_ptr<Sandbox> createSandboxDarwin(){
    return std::make_unique<SandboxDarwin>();
};

}
