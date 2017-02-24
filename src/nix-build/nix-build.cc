#include <cstring>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <vector>

#include <unistd.h>

#include "store-api.hh"
#include "globals.hh"
#include "derivations.hh"
#include "affinity.hh"
#include "util.hh"
#include "shared.hh"

using namespace nix;

extern char * * environ;

/* Recreate the effect of the perl shellwords function, breaking up a
 * string into arguments like a shell word, including escapes
 */
std::vector<string> shellwords(const string & s)
{
    std::regex whitespace("^(\\s+).*");
    auto begin = s.cbegin();
    std::vector<string> res;
    std::string cur;
    enum state {
        sBegin,
        sQuote
    };
    state st = sBegin;
    auto it = begin;
    for (; it != s.cend(); ++it) {
        if (st == sBegin) {
            std::smatch match;
            if (regex_search(it, s.cend(), match, whitespace)) {
                cur.append(begin, it);
                res.push_back(cur);
                cur.clear();
                it = match[1].second;
                begin = it;
            }
        }
        switch (*it) {
            case '"':
                cur.append(begin, it);
                begin = it + 1;
                st = st == sBegin ? sQuote : sBegin;
                break;
            case '\\':
                /* perl shellwords mostly just treats the next char as part of the string with no special processing */
                cur.append(begin, it);
                begin = ++it;
                break;
        }
    }
    cur.append(begin, it);
    if (!cur.empty()) res.push_back(cur);
    return res;
}

static void maybePrintExecError(ExecError & e)
{
    if (WIFEXITED(e.status))
        throw Exit(WEXITSTATUS(e.status));
    else
        throw e;
}

int main(int argc, char ** argv)
{
    return handleExceptions(argv[0], [&]() {
        initNix();
        auto store = openStore();
        auto dryRun = false;
        auto verbose = false;
        auto runEnv = std::regex_search(argv[0], std::regex("nix-shell$"));
        auto pure = false;
        auto fromArgs = false;
        auto packages = false;
        // Same condition as bash uses for interactive shells
        auto interactive = isatty(STDIN_FILENO) && isatty(STDERR_FILENO);

        Strings instArgs;
        Strings buildArgs;
        Strings exprs;

        auto shell = getEnv("SHELL", "/bin/sh");
        std::string envCommand; // interactive shell
        Strings envExclude;

        auto myName = runEnv ? "nix-shell" : "nix-build";

        auto inShebang = false;
        std::string script;
        std::vector<string> savedArgs;

        AutoDelete tmpDir(createTempDir("", myName));

        std::string outLink = "./result";
        auto drvLink = (Path) tmpDir + "/derivation";

        std::vector<string> args;
        for (int i = 1; i < argc; ++i)
            args.push_back(argv[i]);

        // Heuristic to see if we're invoked as a shebang script, namely, if we
        // have a single argument, it's the name of an executable file, and it
        // starts with "#!".
        if (runEnv && argc > 1 && !std::regex_search(argv[1], std::regex("nix-shell"))) {
            script = argv[1];
            if (access(script.c_str(), F_OK) == 0 && access(script.c_str(), X_OK) == 0) {
                auto lines = tokenizeString<Strings>(readFile(script), "\n");
                if (std::regex_search(lines.front(), std::regex("^#!"))) {
                    lines.pop_front();
                    inShebang = true;
                    for (int i = 2; i < argc; ++i)
                        savedArgs.push_back(argv[i]);
                    args.clear();
                    for (auto line : lines) {
                        line = chomp(line);
                        std::smatch match;
                        if (std::regex_match(line, match, std::regex("^#!\\s*nix-shell (.*)$")))
                            for (const auto & word : shellwords(match[1].str()))
                                args.push_back(word);
                    }
                }
            }
        }

        for (size_t n = 0; n < args.size(); ++n) {
            auto arg = args[n];

            if (arg == "--help") {
                deletePath(tmpDir);
                showManPage(myName);
            }

            else if (arg == "--version")
                printVersion(myName);

            else if (arg == "--add-drv-link") {
                drvLink = "./derivation";
            }

            else if (arg == "--no-out-link" || arg == "--no-link") {
                outLink = (Path) tmpDir + "/result";
            }

            else if (arg == "--drv-link") {
                n++;
                if (n >= args.size()) {
                    throw UsageError("--drv-link requires an argument");
                }
                drvLink = args[n];
            }

            else if (arg == "--out-link" || arg == "-o") {
                n++;
                if (n >= args.size()) {
                    throw UsageError(format("%1% requires an argument") % arg);
                }
                outLink = args[n];
            }

            else if (arg == "--attr" || arg == "-A" || arg == "-I") {
                n++;
                if (n >= args.size()) {
                    throw UsageError(format("%1% requires an argument") % arg);
                }
                instArgs.push_back(arg);
                instArgs.push_back(args[n]);
            }

            else if (arg == "--arg" || arg == "--argstr") {
                if (n + 2 >= args.size()) {
                    throw UsageError(format("%1% requires two arguments") % arg);
                }
                instArgs.push_back(arg);
                instArgs.push_back(args[n + 1]);
                instArgs.push_back(args[n + 2]);
                n += 2;
            }

            else if (arg == "--option") {
                if (n + 2 >= args.size()) {
                    throw UsageError(format("%1% requires two arguments") % arg);
                }
                instArgs.push_back(arg);
                instArgs.push_back(args[n + 1]);
                instArgs.push_back(args[n + 2]);
                buildArgs.push_back(arg);
                buildArgs.push_back(args[n + 1]);
                buildArgs.push_back(args[n + 2]);
                n += 2;
            }

            else if (arg == "--max-jobs" || arg == "-j" || arg == "--max-silent-time" || arg == "--cores" || arg == "--timeout" || arg == "--add-root") {
                n++;
                if (n >= args.size()) {
                    throw UsageError(format("%1% requires an argument") % arg);
                }
                buildArgs.push_back(arg);
                buildArgs.push_back(args[n]);
            }

            else if (arg == "--dry-run") {
                buildArgs.push_back("--dry-run");
                dryRun = true;
            }

            else if (arg == "--show-trace") {
                instArgs.push_back(arg);
            }

            else if (arg == "-") {
                exprs = Strings{"-"};
            }

            else if (arg == "--verbose" || (arg.size() >= 2 && arg.substr(0, 2) == "-v")) {
                buildArgs.push_back(arg);
                instArgs.push_back(arg);
                verbose = true;
            }

            else if (arg == "--quiet" || arg == "--repair") {
                buildArgs.push_back(arg);
                instArgs.push_back(arg);
            }

            else if (arg == "--check") {
                buildArgs.push_back(arg);
            }

            else if (arg == "--run-env") { // obsolete
                runEnv = true;
            }

            else if (arg == "--command" || arg == "--run") {
                n++;
                if (n >= args.size()) {
                    throw UsageError(format("%1% requires an argument") % arg);
                }
                envCommand = args[n] + "\nexit";
                if (arg == "--run")
                    interactive = false;
            }

            else if (arg == "--exclude") {
                n++;
                if (n >= args.size()) {
                    throw UsageError(format("%1% requires an argument") % arg);
                }
                envExclude.push_back(args[n]);
            }

            else if (arg == "--pure") { pure = true; }
            else if (arg == "--impure") { pure = false; }

            else if (arg == "--expr" || arg == "-E") {
                fromArgs = true;
                instArgs.push_back("--expr");
            }

            else if (arg == "--packages" || arg == "-p") {
                packages = true;
            }

            else if (inShebang && arg == "-i") {
                n++;
                if (n >= args.size()) {
                    throw UsageError(format("%1% requires an argument") % arg);
                }
                interactive = false;
                auto interpreter = args[n];
                auto execArgs = "";

                auto shellEscape = [](const string & s) {
                    return "'" + std::regex_replace(s, std::regex("'"), "'\\''") + "'";
                };

                // Überhack to support Perl. Perl examines the shebang and
                // executes it unless it contains the string "perl" or "indir",
                // or (undocumented) argv[0] does not contain "perl". Exploit
                // the latter by doing "exec -a".
                if (std::regex_search(interpreter, std::regex("perl")))
                    execArgs = "-a PERL";

                std::ostringstream joined;
                for (const auto & i : savedArgs)
                    joined << shellEscape(i) << ' ';

                if (std::regex_search(interpreter, std::regex("ruby"))) {
                    // Hack for Ruby. Ruby also examines the shebang. It tries to
                    // read the shebang to understand which packages to read from. Since
                    // this is handled via nix-shell -p, we wrap our ruby script execution
                    // in ruby -e 'load' which ignores the shebangs.
                    envCommand = (format("exec %1% %2% -e 'load(\"%3%\") -- %4%") % execArgs % interpreter % script % joined.str()).str();
                } else {
                    envCommand = (format("exec %1% %2% %3% %4%") % execArgs % interpreter % script % joined.str()).str();
                }
            }

            else if (!arg.empty() && arg[0] == '-') {
                buildArgs.push_back(arg);
            }

            else if (arg == "-Q" || arg == "--no-build-output") {
                buildArgs.push_back(arg);
                instArgs.push_back(arg);
            }

            else {
                exprs.push_back(arg);
            }
        }

        if (packages && fromArgs) {
            throw UsageError("‘-p’ and ‘-E’ are mutually exclusive");
        }

        if (packages) {
            instArgs.push_back("--expr");
            std::ostringstream joined;
            joined << "with import <nixpkgs> { }; runCommand \"shell\" { buildInputs = [ ";
            for (const auto & i : exprs)
                joined << '(' << i << ") ";
            joined << "]; } \"\"";
            exprs = Strings{joined.str()};
        } else if (!fromArgs) {
            if (exprs.empty() && runEnv && access("shell.nix", F_OK) == 0)
                exprs.push_back("shell.nix");
            if (exprs.empty())
                exprs.push_back("default.nix");
        }

        if (runEnv)
            setenv("IN_NIX_SHELL", pure ? "pure" : "impure", 1);

        for (auto & expr : exprs) {
            // Instantiate.
            std::vector<string> drvPaths;
            if (!std::regex_match(expr, std::regex("^/.*\\.drv$"))) {
                // If we're in a #! script, interpret filenames relative to the
                // script.
                if (inShebang && !packages)
                    expr = absPath(expr, dirOf(script));

                Strings instantiateArgs{"--add-root", drvLink, "--indirect"};
                for (const auto & arg : instArgs)
                    instantiateArgs.push_back(arg);
                instantiateArgs.push_back(expr);
                try {
                    auto instOutput = runProgram(settings.nixBinDir + "/nix-instantiate", false, instantiateArgs);
                    drvPaths = tokenizeString<std::vector<string>>(instOutput);
                } catch (ExecError & e) {
                    maybePrintExecError(e);
                }
            } else {
                drvPaths.push_back(expr);
            }

            if (runEnv) {
                if (drvPaths.size() != 1)
                    throw UsageError("a single derivation is required");
                auto drvPath = drvPaths[0];
                drvPath = drvPath.substr(0, drvPath.find_first_of('!'));
                if (isLink(drvPath))
                    drvPath = readLink(drvPath);
                auto drv = store->derivationFromPath(drvPath);

                // Build or fetch all dependencies of the derivation.
                Strings nixStoreArgs{"-r", "--no-output", "--no-gc-warning"};
                for (const auto & arg : buildArgs)
                    nixStoreArgs.push_back(arg);
                for (const auto & input : drv.inputDrvs)
                    if (std::all_of(envExclude.cbegin(), envExclude.cend(), [&](const string & exclude) { return !std::regex_search(input.first, std::regex(exclude)); }))
                        nixStoreArgs.push_back(input.first);
                for (const auto & src : drv.inputSrcs)
                    nixStoreArgs.push_back(src);

                try {
                    runProgram(settings.nixBinDir + "/nix-store", false, nixStoreArgs);
                } catch (ExecError & e) {
                    maybePrintExecError(e);
                }

                // Set the environment.
                auto env = getEnv();

                auto tmp = getEnv("TMPDIR", getEnv("XDG_RUNTIME_DIR", "/tmp"));

                if (pure) {
                    std::set<string> keepVars{"HOME", "USER", "LOGNAME", "DISPLAY", "PATH", "TERM", "IN_NIX_SHELL", "TZ", "PAGER", "NIX_BUILD_SHELL"};
                    decltype(env) newEnv;
                    for (auto & i : env)
                        if (keepVars.count(i.first))
                            newEnv.emplace(i);
                    env = newEnv;
                    // NixOS hack: prevent /etc/bashrc from sourcing /etc/profile.
                    env["__ETC_PROFILE_SOURCED"] = "1";
                }

                env["NIX_BUILD_TOP"] = env["TMPDIR"] = env["TEMPDIR"] = env["TMP"] = env["TEMP"] = tmp;
                env["NIX_STORE"] = store->storeDir;

                for (auto & var : drv.env)
                    env[var.first] = var.second;

                restoreAffinity();

                // Run a shell using the derivation's environment.  For
                // convenience, source $stdenv/setup to setup additional
                // environment variables and shell functions.  Also don't lose
                // the current $PATH directories.
                auto rcfile = (Path) tmpDir + "/rc";
                writeFile(rcfile, fmt(
                        "rm -rf '%1%'; "
                        "[ -n \"$PS1\" ] && [ -e ~/.bashrc ] && source ~/.bashrc; "
                        "%2%"
                        "dontAddDisableDepTrack=1; "
                        "[ -e $stdenv/setup ] && source $stdenv/setup; "
                        "%3%"
                        "set +e; "
                        "[ -n \"$PS1\" ] && PS1=\"\\n\\[\\033[1;32m\\][nix-shell:\\w]$\\[\\033[0m\\] \"; "
                        "if [ \"$(type -t runHook)\" = function ]; then runHook shellHook; fi; "
                        "unset NIX_ENFORCE_PURITY; "
                        "unset NIX_INDENT_MAKE; "
                        "shopt -u nullglob; "
                        "unset TZ; %4%"
                        "%5%",
                        (Path) tmpDir,
                        (pure ? "" : "p=$PATH; "),
                        (pure ? "" : "PATH=$PATH:$p; unset p; "),
                        (getenv("TZ") ? (string("export TZ='") + getenv("TZ") + "'; ") : ""),
                        envCommand));

                Strings envStrs;
                for (auto & i : env)
                    envStrs.push_back(i.first + "=" + i.second);

                auto args = interactive
                    ? Strings{"bash", "--rcfile", rcfile}
                    : Strings{"bash", rcfile};

                auto envPtrs = stringsToCharPtrs(envStrs);

                auto shell = getEnv("NIX_BUILD_SHELL", "bash");

                environ = envPtrs.data();

                auto argPtrs = stringsToCharPtrs(args);

                restoreSignals();

                execvp(shell.c_str(), argPtrs.data());

                throw SysError("executing shell ‘%s’", shell);
            }

            // Ugly hackery to make "nix-build -A foo.all" produce symlinks
            // ./result, ./result-dev, and so on, rather than ./result,
            // ./result-2-dev, and so on.  This combines multiple derivation
            // paths into one "/nix/store/drv-path!out1,out2,..." argument.
            std::string prevDrvPath;
            Strings drvPaths2;
            for (const auto & drvPath : drvPaths) {
                auto p = drvPath;
                std::string output = "out";
                std::smatch match;
                if (std::regex_match(drvPath, match, std::regex("(.*)!(.*)"))) {
                    p = match[1].str();
                    output = match[2].str();
                }
                auto target = readLink(p);
                if (verbose)
                    std::cerr << "derivation is " << target << '\n';
                if (target == prevDrvPath) {
                    auto last = drvPaths2.back();
                    drvPaths2.pop_back();
                    drvPaths2.push_back(last + "," + output);
                } else {
                    drvPaths2.push_back(target + "!" + output);
                    prevDrvPath = target;
                }
            }
            // Build.
            Strings outPaths;
            Strings nixStoreArgs{"--add-root", outLink, "--indirect", "-r"};
            for (const auto & arg : buildArgs)
                nixStoreArgs.push_back(arg);
            for (const auto & path : drvPaths2)
                nixStoreArgs.push_back(path);

            std::string nixStoreRes;
            try {
                nixStoreRes = runProgram(settings.nixBinDir + "/nix-store", false, nixStoreArgs);
            } catch (ExecError & e) {
                maybePrintExecError(e);
            }

            for (const auto & outpath : tokenizeString<std::vector<string>>(nixStoreRes))
                outPaths.push_back(chomp(outpath));

            if (dryRun)
                continue;

            for (const auto & outPath : outPaths)
                std::cout << readLink(outPath) << '\n';
        }
    });
}
