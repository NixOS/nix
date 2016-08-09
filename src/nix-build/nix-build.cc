#include <cstring>
#include <regex>
#include "util.hh"
#include <unistd.h>
#include "shared.hh"
#include <sstream>
#include <vector>
#include <iostream>
#include <fstream>
#include "store-api.hh"
#include "globals.hh"
#include "derivations.hh"

using namespace nix;
using std::stringstream;

extern char ** environ;

std::vector<string> shellwords(const string & s)
{
    auto whitespace = std::regex("^(\\s+).*");
    auto begin = s.cbegin();
    auto res = std::vector<string>{};
    auto cur = stringstream{};
    enum state {
        sBegin,
        sQuote
    };
    state st = sBegin;
    auto it = begin;
    for (; it != s.cend(); ++it) {
        if (st == sBegin) {
            auto match = std::smatch{};
            if (regex_search(it, s.cend(), match, whitespace)) {
                cur << string(begin, it);
                res.push_back(cur.str());
                cur = stringstream{};
                it = match[1].second;
                begin = it;
            }
        }
        switch (*it) {
            case '"':
                cur << string(begin, it);
                begin = it + 1;
                st = st == sBegin ? sQuote : sBegin;
                break;
            case '\\':
                /* perl shellwords mostly just treats the next char as part of the string with no special processing */
                cur << string(begin, it);
                begin = ++it;
                break;
        }
    }
    cur << string(begin, it);
    auto last = cur.str();
    if (!last.empty()) {
        res.push_back(std::move(last));
    }
    return res;
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
        auto interactive = true;

        auto instArgs = Strings{};
        auto buildArgs = Strings{};
        auto exprs = Strings{};

        auto shell = getEnv("SHELL", "/bin/sh");
        auto envCommand = string{}; // interactive shell
        auto envExclude = Strings{};

        auto myName = runEnv ? "nix-shell" : "nix-build";

        auto inShebang = false;
        auto script = string{};
        auto savedArgs = std::vector<string>{};

        auto tmpDir = AutoDelete{createTempDir("", myName)};

        auto outLink = string("./result");
        auto drvLink = (Path) tmpDir + "/derivation";

        auto args = std::vector<string>{};
        for (int i = 1; i < argc; ++i)
            args.push_back(argv[i]);
        // Heuristic to see if we're invoked as a shebang script, namely, if we
        // have a single argument, it's the name of an executable file, and it
        // starts with "#!".
        if (runEnv && argc > 1 && !std::regex_search(argv[1], std::regex("nix-shell"))) {
            script = argv[1];
            if (access(script.c_str(), F_OK) == 0 && access(script.c_str(), X_OK) == 0) {
                auto SCRIPT = std::ifstream(script);
                string first;
                std::getline(SCRIPT, first);
                if (std::regex_search(first, std::regex("^#!"))) {
                    inShebang = true;
                    for (int i = 2; i < argc - 1; ++i)
                        savedArgs.push_back(argv[i]);
                    args = std::vector<string>{};
                    for (string line; std::getline(SCRIPT, line);) {
                        line = chomp(line);
                        std::smatch match;
                        if (std::regex_match(line, match, std::regex("^#!\\s*nix-shell (.*)$")))
                            for (const auto & word : shellwords(match[1].str()))
                                args.push_back(word);
                    }
                }
            }
        }

        for (auto n = decltype(args)::size_type{0}; n < args.size(); ++n) {
            auto arg = args[n];

            if (arg == "--help") {
                deletePath(tmpDir);
                tmpDir.cancel();
                execlp("man", "man", myName, NULL);
                throw SysError("executing man");
            }

            else if (arg == "--version") {
                std::cout << myName << " (Nix) " << nixVersion << '\n';
                return;
            }

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
                auto interpreter = args[n];
                auto execArgs = "";

                auto shellEscape = [](const string & s) {
                    return "'" + std::regex_replace(s, std::regex("'"), "'\\''") + "'";
                };

                // Überhack to support Perl. Perl examines the shebang and
                // executes it unless it contains the string "perl" or "indir",
                // or (undocumented) argv[0] does not contain "perl". Exploit
                // the latter by doing "exec -a".
                if (std::regex_search(interpreter, std::regex("perl"))) {
                        execArgs = "-a PERL";
                }

                auto joined = stringstream{};
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
            auto joined = stringstream{};
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
            setenv("IN_NIX_SHELL", "1", 1);

        for (auto & expr : exprs) {
            // Instantiate.
            auto drvPaths = std::vector<string>{};
            if (!std::regex_match(expr, std::regex("^/.*\\.drv$"))) {
                // If we're in a #! script, interpret filenames relative to the
                // script.
                if (inShebang && !packages)
                    expr = absPath(expr, dirOf(script));

                auto instantiateArgs = Strings{"--add-root", drvLink, "--indirect"};
                for (const auto & arg : instArgs)
                    instantiateArgs.push_back(arg);
                instantiateArgs.push_back(expr);
                auto instOutput = runProgram(settings.nixBinDir + "/nix-instantiate", false, instantiateArgs);
                drvPaths = tokenizeString<std::vector<string>>(instOutput);
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
                auto nixStoreArgs = Strings{"-r", "--no-output", "--no-gc-warning"};
                for (const auto & arg : buildArgs)
                    nixStoreArgs.push_back(arg);
                for (const auto & input : drv.inputDrvs)
                    if (std::all_of(envExclude.cbegin(), envExclude.cend(), [&](const string & exclude) { return !std::regex_search(input.first, std::regex(exclude)); }))
                        nixStoreArgs.push_back(input.first);
                for (const auto & src : drv.inputSrcs)
                    nixStoreArgs.push_back(src);
                runProgram(settings.nixBinDir + "/nix-store", false, nixStoreArgs);

                // Set the environment.
                auto tmp = getEnv("TMPDIR", getEnv("XDG_RUNTIME_DIR", "/tmp"));
                if (pure) {
                    auto skippedEnv = std::vector<string>{"HOME", "USER", "LOGNAME", "DISPLAY", "PATH", "TERM", "IN_NIX_SHELL", "TZ", "PAGER", "NIX_BUILD_SHELL"};
                    auto removed = std::vector<string>{};
                    for (auto i = size_t{0}; environ[i]; ++i) {
                        auto eq = strchr(environ[i], '=');
                        if (!eq)
                            // invalid env, just keep going
                            continue;
                        auto name = string(environ[i], eq);
                        if (find(skippedEnv.begin(), skippedEnv.end(), name) == skippedEnv.end())
                            removed.emplace_back(std::move(name));
                    }
                    for (const auto & name : removed)
                        unsetenv(name.c_str());
                    // NixOS hack: prevent /etc/bashrc from sourcing /etc/profile.
                    setenv("__ETC_PROFILE_SOURCED", "1", 1);
                }
                setenv("NIX_BUILD_TOP", tmp.c_str(), 1);
                setenv("TMPDIR", tmp.c_str(), 1);
                setenv("TEMPDIR", tmp.c_str(), 1);
                setenv("TMP", tmp.c_str(), 1);
                setenv("TEMP", tmp.c_str(), 1);
                setenv("NIX_STORE", store->storeDir.c_str(), 1);
                for (const auto & env : drv.env)
                    setenv(env.first.c_str(), env.second.c_str(), 1);

                // Run a shell using the derivation's environment.  For
                // convenience, source $stdenv/setup to setup additional
                // environment variables and shell functions.  Also don't lose
                // the current $PATH directories.
                auto rcfile = (Path) tmpDir + "/rc";
                writeFile(rcfile, (format(
                        "rm -rf '%1%'; "
                        "unset BASH_ENV; "
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
                        "%5%"
                        ) % (Path) tmpDir % (pure ? "" : "p=$PATH") % (pure ? "" : "PATH=$PATH:$p; unset p; ") % (getenv("TZ") ? (string("export TZ='") + getenv("TZ") + "'; ") : "") % envCommand).str());
                setenv("BASH_ENV", rcfile.c_str(), 1);
                if (interactive)
                    execlp(getEnv("NIX_BUILD_SHELL", "bash").c_str(), "bash", "--rcfile", rcfile.c_str(), NULL);
                else
                    execlp(getEnv("NIX_BUILD_SHELL", "bash").c_str(), "bash", rcfile.c_str(), NULL);
                throw SysError("executing shell");
            }

            // Ugly hackery to make "nix-build -A foo.all" produce symlinks
            // ./result, ./result-dev, and so on, rather than ./result,
            // ./result-2-dev, and so on.  This combines multiple derivation
            // paths into one "/nix/store/drv-path!out1,out2,..." argument.
            auto prevDrvPath = string{};
            auto drvPaths2 = Strings{};
            for (const auto & drvPath : drvPaths) {
                auto p = drvPath;
                auto output = string{"out"};
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
            auto outPaths = Strings{};
            auto nixStoreArgs = Strings{"--add-root", outLink, "--indirect", "-r"};
            for (const auto & arg : buildArgs)
                nixStoreArgs.push_back(arg);
            for (const auto & path : drvPaths2)
                nixStoreArgs.push_back(path);
            auto nixStoreRes = runProgram(settings.nixBinDir + "/nix-store", false, nixStoreArgs);
            for (const auto & outpath : tokenizeString<std::vector<string>>(nixStoreRes)) {
                outPaths.push_back(chomp(outpath));
            }

            if (dryRun)
                continue;
            for (const auto & outPath : outPaths) {
                auto target = readLink(outPath);
                std::cout << target << '\n';
            }
        }
        return;
    });
}

