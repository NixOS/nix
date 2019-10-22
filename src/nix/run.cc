#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"
#include "derivations.hh"
#include "local-store.hh"
#include "finally.hh"
#include "fs-accessor.hh"
#include "progress-bar.hh"
#include "affinity.hh"

#if __linux__
#include <sys/mount.h>
#endif

#include <queue>

using namespace nix;

std::string chrootHelperName = "__run_in_chroot";

struct CmdRun : InstallablesCommand
{
    std::vector<std::string> command = { "bash" };
    StringSet keep, unset;
    bool ignoreEnvironment = false;

    CmdRun()
    {
        mkFlag()
            .longName("command")
            .shortName('c')
            .description("command and arguments to be executed; defaults to 'bash'")
            .labels({"command", "args"})
            .arity(ArityAny)
            .handler([&](std::vector<std::string> ss) {
                if (ss.empty()) throw UsageError("--command requires at least one argument");
                command = ss;
            });

        mkFlag()
            .longName("ignore-environment")
            .shortName('i')
            .description("clear the entire environment (except those specified with --keep)")
            .set(&ignoreEnvironment, true);

        mkFlag()
            .longName("keep")
            .shortName('k')
            .description("keep specified environment variable")
            .arity(1)
            .labels({"name"})
            .handler([&](std::vector<std::string> ss) { keep.insert(ss.front()); });

        mkFlag()
            .longName("unset")
            .shortName('u')
            .description("unset specified environment variable")
            .arity(1)
            .labels({"name"})
            .handler([&](std::vector<std::string> ss) { unset.insert(ss.front()); });
    }

    std::string name() override
    {
        return "run";
    }

    std::string description() override
    {
        return "run a shell in which the specified packages are available";
    }

    Examples examples() override
    {
        return {
            Example{
                "To start a shell providing GNU Hello from NixOS 17.03:",
                "nix run -f channel:nixos-17.03 hello"
            },
            Example{
                "To start a shell providing youtube-dl from your 'nixpkgs' channel:",
                "nix run nixpkgs.youtube-dl"
            },
            Example{
                "To run GNU Hello:",
                "nix run nixpkgs.hello -c hello --greeting 'Hi everybody!'"
            },
            Example{
                "To run GNU Hello in a chroot store:",
                "nix run --store ~/my-nix nixpkgs.hello -c hello"
            },
        };
    }

    void run(ref<Store> store) override
    {
        auto outPaths = toStorePaths(store, Build, installables);

        auto accessor = store->getFSAccessor();

#ifndef _WIN32
        if (ignoreEnvironment) {

            if (!unset.empty())
                throw UsageError("--unset does not make sense with --ignore-environment");

            std::map<std::string, std::string> kept;
            for (auto & var : keep) {
                auto s = getenv(var.c_str());
                if (s) kept[var] = s;
            }

            clearEnv();

            for (auto & var : kept)
                setenv(var.first.c_str(), var.second.c_str(), 1);

        } else {

            if (!keep.empty())
                throw UsageError("--keep does not make sense without --ignore-environment");

            for (auto & var : unset)
                unsetenv(var.c_str());
        }

        std::unordered_set<Path> done;
        std::queue<Path> todo;
        for (auto & path : outPaths) todo.push(path);

        auto unixPath = tokenizeString<Strings>(getEnv("PATH"), ":");

        while (!todo.empty()) {
            Path path = todo.front();
            todo.pop();
            if (!done.insert(path).second) continue;

            if (true)
                unixPath.push_front(path + "/bin");

            auto propPath = path + "/nix-support/propagated-user-env-packages";
            if (accessor->stat1(propPath).type == FSAccessor::tRegular) {
                for (auto & p : tokenizeString<Paths>(readFile(propPath)))
                    todo.push(p);
            }
        }

        setenv("PATH", concatStringsSep(":", unixPath).c_str(), 1);

        std::string cmd = *command.begin();
        Strings args;
        for (auto & arg : command) args.push_back(arg);

        stopProgressBar();

        restoreSignals();

        restoreAffinity();

        /* If this is a diverted store (i.e. its "logical" location
           (typically /nix/store) differs from its "physical" location
           (e.g. /home/eelco/nix/store), then run the command in a
           chroot. For non-root users, this requires running it in new
           mount and user namespaces. Unfortunately,
           unshare(CLONE_NEWUSER) doesn't work in a multithreaded
           program (which "nix" is), so we exec() a single-threaded
           helper program (chrootHelper() below) to do the work. */
        auto store2 = store.dynamic_pointer_cast<LocalStore>();

        if (store2 && store->storeDir != store2->realStoreDir) {
            Strings helperArgs = { chrootHelperName, store->storeDir, store2->realStoreDir, cmd };
            for (auto & arg : args) helperArgs.push_back(arg);

            execv(readLink("/proc/self/exe").c_str(), stringsToCharPtrs(helperArgs).data());

            throw PosixError("could not execute chroot helper");
        }

        execvp(cmd.c_str(), stringsToCharPtrs(args).data());

        throw PosixError("unable to exec '%s'", cmd);
#else
        std::map<std::wstring, std::wstring> uenv;

        if (ignoreEnvironment) {

            if (!unset.empty())
                throw UsageError("--unset does not make sense with --ignore-environment");

            std::map<std::wstring, std::wstring> kept;
            for (auto & var : keep) {
                std::wstring s = getEnvW(from_bytes(var), L"<not-found>");
                if (s != L"<not-found>")
                    uenv[from_bytes(var)] = s;
            }
        } else {

            if (!keep.empty())
                throw UsageError("--keep does not make sense without --ignore-environment");

            for (auto & e : getEntireEnvW()) {
                if (unset.find(to_bytes(e.first)) == unset.end())
                    uenv[e.first] = e.second;
            }
        }

        std::unordered_set<Path> done;
        std::queue<Path> todo;
        for (auto & path : outPaths) {
            std::cerr << "path=[" << path << "]" << std::endl;
            todo.push(path);
        }
        auto windowsPath = tokenizeString<Strings>(getEnv("PATH"), ";");

        while (!todo.empty()) {
            Path path = todo.front();
            todo.pop();
            if (!done.insert(path).second) continue;

            windowsPath.push_front(path + "/bin");

            auto propPath = path + "/nix-support/propagated-user-env-packages";
            if (accessor->stat1(propPath).type == FSAccessor::tRegular) {
                for (auto & p : tokenizeString<Paths>(readFile(propPath)))
                    todo.push(p);
            }
        }
        for (auto & path : windowsPath) {
            std::cerr << "win=[" << path << "]" << std::endl;
        }

        for (auto & arg : command) {
            std::cerr << "arg=[" << arg << "]" << std::endl;
            //args.push_back(arg);
        }

        stopProgressBar();

        restoreAffinity();


        const std::string program = *command.begin();
        assert(program.find('/') == string::npos);
        assert(program.find('\\') == string::npos);

        Path executable;
        bool found = false;
        bool checkShebangs = false;
        for (const std::string & outpath : /*windowsPath*/outPaths) {
//          std::cerr << "_________________outpath='" << outpath << "'" << std::endl;
            assert(outpath == canonPath(outpath));
            Path candidate = outpath + "/bin/" + program;
            if (pathExists(candidate       )) { executable = candidate       ; found = true; checkShebangs = true;  break; }
            if (pathExists(candidate+".exe")) { executable = candidate+".exe"; found = true; checkShebangs = false; break; }
            if (pathExists(candidate+".cmd")) { executable = candidate+".cmd"; found = true; checkShebangs = false; break; }
            if (pathExists(candidate+".bat")) { executable = candidate+".bat"; found = true; checkShebangs = false; break; }
        }
        if (!found)
            throw Error("executable '%1%' not found in outpaths", executable);
//      std::cerr << "_________________executable='" << executable << "'" << std::endl;

        string shebang;
        if (checkShebangs) {
          std::vector<char> buf(512);
          {
              AutoCloseWindowsHandle fd = CreateFileW(pathW(executable).c_str(), GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
              if (fd.get() == INVALID_HANDLE_VALUE)
                  throw WinError("CreateFileW '%1%'", executable);

              DWORD filled = 0;
              while (filled < buf.size()) {
                  DWORD n;
                  if (!ReadFile(fd.get(), buf.data() + filled, buf.size() - filled, &n, NULL))
                      throw WinError("ReadFile '%1%'", executable);
                  if (n == 0)
                      break;
                  filled += n;
              }
              buf.resize(filled);
          }
          if (buf.size() < 3)
              throw Error("executable '%1%' is too small", executable);
          if (buf[0] == '#' && buf[1] == '!') {
              std::vector<char>::const_iterator lf = std::find(buf.begin(), buf.end(), '\n');
              if (lf == buf.end())
                  throw Error("executable '%1%' shebang is too long", executable);

              shebang = trim(std::string(buf.data()+2, lf-buf.begin()-2));
//            std::cerr << "_________________shebang1='" << shebang << "'" << std::endl;

              if (shebang.empty())
                  throw Error("executable '%1%' shebang is empty", executable);

              // BUGBUG: msys hack, remove later
              if (shebang[0] == '/') {
                  shebang = trim(runProgramGetStdout("cygpath", true, {"-m", shebang}));
//                std::cerr << "_________________shebang2='" << shebang << "'" << std::endl;
              }
              assert(!shebang.empty() && shebang[0] != '/');

              if (!pathExists(shebang))
                  throw Error("executable '%1%' shebang '%2%' does not exist", executable, shebang);
          }
        }

        std::wstring ucmdline;
        if (!shebang.empty())
            ucmdline = windowsEscapeW(from_bytes(shebang)) + L' ';
        ucmdline += windowsEscapeW(from_bytes(executable));
        for (auto v = command.begin()+1; v != command.end(); ++v) {
            ucmdline += L' ';
            ucmdline += windowsEscapeW(from_bytes(*v));
        }

        std::wstring uenvline;
        for (auto & i : uenv)
            uenvline += i.first + L'=' + i.second + L'\0';
        uenvline += L'\0';

//      std::cerr << "_________________executable='" << to_bytes(pathW(executable)) << "'" << std::endl;
//      std::cerr << "_________________shebang='"    <<         (shebang)           << "'" << std::endl;
//      std::cerr << "_________________ucmdline='"   << to_bytes(ucmdline)          << "'" << std::endl;

        STARTUPINFOW si = {0};
        si.cb = sizeof(STARTUPINFOW);
        PROCESS_INFORMATION pi = {0};
        if (!CreateProcessW(
            pathW(shebang.empty() ? executable : shebang).c_str(),         // LPCWSTR               lpApplicationName,
            const_cast<wchar_t*>((ucmdline).c_str()),                      // LPWSTR                lpCommandLine,
            NULL,                                                          // LPSECURITY_ATTRIBUTES lpProcessAttributes,
            NULL,                                                          // LPSECURITY_ATTRIBUTES lpThreadAttributes,
            TRUE,                                                          // BOOL                  bInheritHandles,
            CREATE_UNICODE_ENVIRONMENT,                                    // DWORD                 dwCreationFlags,
            const_cast<wchar_t*>(uenvline.c_str()),
            NULL,                                                          // LPCWSTR               lpCurrentDirectory,
            &si,                                                           // LPSTARTUPINFOW        lpStartupInfo,
            &pi                                                            // LPPROCESS_INFORMATION lpProcessInformation
        )) {
            throw WinError("CreateProcessW(%1%)", to_bytes(ucmdline));
        }
        CloseHandle(pi.hThread);

        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
#endif
    }
};

static RegisterCommand r1(make_ref<CmdRun>());

void chrootHelper(int argc, char * * argv)
{
    int p = 1;
    std::string storeDir = argv[p++];
    std::string realStoreDir = argv[p++];
    std::string cmd = argv[p++];
    Strings args;
    while (p < argc)
        args.push_back(argv[p++]);

#if __linux__
    uid_t uid = getuid();
    uid_t gid = getgid();

    if (unshare(CLONE_NEWUSER | CLONE_NEWNS) == -1)
        /* Try with just CLONE_NEWNS in case user namespaces are
           specifically disabled. */
        if (unshare(CLONE_NEWNS) == -1)
            throw PosixError("setting up a private mount namespace");

    /* Bind-mount realStoreDir on /nix/store. If the latter mount
       point doesn't already exists, we have to create a chroot
       environment containing the mount point and bind mounts for the
       children of /. Would be nice if we could use overlayfs here,
       but that doesn't work in a user namespace yet (Ubuntu has a
       patch for this:
       https://bugs.launchpad.net/ubuntu/+source/linux/+bug/1478578). */
    if (!pathExists(storeDir)) {
        // FIXME: Use overlayfs?

        Path tmpDir = createTempDir();

        createDirs(tmpDir + storeDir);

        if (mount(realStoreDir.c_str(), (tmpDir + storeDir).c_str(), "", MS_BIND, 0) == -1)
            throw PosixError("mounting '%s' on '%s'", realStoreDir, storeDir);

        for (auto entry : readDirectory("/")) {
            auto src = "/" + entry.name;
            auto st = lstatPath(src);
            if (!S_ISDIR(st.st_mode)) continue;
            Path dst = tmpDir + "/" + entry.name;
            if (pathExists(dst)) continue;
            if (mkdir(dst.c_str(), 0700) == -1)
                throw PosixError("creating directory '%s'", dst);
            if (mount(src.c_str(), dst.c_str(), "", MS_BIND | MS_REC, 0) == -1)
                throw PosixError("mounting '%s' on '%s'", src, dst);
        }

        char * cwd = getcwd(0, 0);
        if (!cwd) throw PosixError("getting current directory");
        Finally freeCwd([&]() { free(cwd); });

        if (chroot(tmpDir.c_str()) == -1)
            throw PosixError(format("chrooting into '%s'") % tmpDir);

        if (chdir(cwd) == -1)
            throw PosixError(format("chdir to '%s' in chroot") % cwd);
    } else
        if (mount(realStoreDir.c_str(), storeDir.c_str(), "", MS_BIND, 0) == -1)
            throw PosixError("mounting '%s' on '%s'", realStoreDir, storeDir);

    writeFile("/proc/self/setgroups", "deny");
    writeFile("/proc/self/uid_map", fmt("%d %d %d", uid, uid, 1));
    writeFile("/proc/self/gid_map", fmt("%d %d %d", gid, gid, 1));

    execvp(cmd.c_str(), stringsToCharPtrs(args).data());

    throw PosixError("unable to exec '%s'", cmd);

#else
    throw Error("mounting the Nix store on '%s' is not supported on this platform", storeDir);
#endif
}
