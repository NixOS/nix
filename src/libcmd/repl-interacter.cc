#include <cstdio>

#ifdef USE_READLINE
#include <readline/history.h>
#include <readline/readline.h>
#else
// editline < 1.15.2 don't wrap their API for C++ usage
// (added in https://github.com/troglobit/editline/commit/91398ceb3427b730995357e9d120539fb9bb7461).
// This results in linker errors due to to name-mangling of editline C symbols.
// For compatibility with these versions, we wrap the API here
// (wrapping multiple times on newer versions is no problem).
extern "C" {
#include <editline.h>
}
#endif

#include "signals.hh"
#include "finally.hh"
#include "repl-interacter.hh"
#include "file-system.hh"
#include "libcmd/repl.hh"

namespace nix {

namespace {
// Used to communicate to NixRepl::getLine whether a signal occurred in ::readline.
volatile sig_atomic_t g_signal_received = 0;

void sigintHandler(int signo)
{
    g_signal_received = signo;
}
};

static detail::ReplCompleterMixin * curRepl; // ugly

static char * completionCallback(char * s, int * match)
{
    auto possible = curRepl->completePrefix(s);
    if (possible.size() == 1) {
        *match = 1;
        auto * res = strdup(possible.begin()->c_str() + strlen(s));
        if (!res)
            throw Error("allocation failure");
        return res;
    } else if (possible.size() > 1) {
        auto checkAllHaveSameAt = [&](size_t pos) {
            auto & first = *possible.begin();
            for (auto & p : possible) {
                if (p.size() <= pos || p[pos] != first[pos])
                    return false;
            }
            return true;
        };
        size_t start = strlen(s);
        size_t len = 0;
        while (checkAllHaveSameAt(start + len))
            ++len;
        if (len > 0) {
            *match = 1;
            auto * res = strdup(std::string(*possible.begin(), start, len).c_str());
            if (!res)
                throw Error("allocation failure");
            return res;
        }
    }

    *match = 0;
    return nullptr;
}

static int listPossibleCallback(char * s, char *** avp)
{
    auto possible = curRepl->completePrefix(s);

    if (possible.size() > (INT_MAX / sizeof(char *)))
        throw Error("too many completions");

    int ac = 0;
    char ** vp = nullptr;

    auto check = [&](auto * p) {
        if (!p) {
            if (vp) {
                while (--ac >= 0)
                    free(vp[ac]);
                free(vp);
            }
            throw Error("allocation failure");
        }
        return p;
    };

    vp = check((char **) malloc(possible.size() * sizeof(char *)));

    for (auto & p : possible)
        vp[ac++] = check(strdup(p.c_str()));

    *avp = vp;

    return ac;
}

ReadlineLikeInteracter::Guard ReadlineLikeInteracter::init(detail::ReplCompleterMixin * repl)
{
    // Allow nix-repl specific settings in .inputrc
    rl_readline_name = "nix-repl";
    try {
        createDirs(dirOf(historyFile));
    } catch (SystemError & e) {
        logWarning(e.info());
    }
#ifndef USE_READLINE
    el_hist_size = 1000;
#endif
    read_history(historyFile.c_str());
    auto oldRepl = curRepl;
    curRepl = repl;
    Guard restoreRepl([oldRepl] { curRepl = oldRepl; });
#ifndef USE_READLINE
    rl_set_complete_func(completionCallback);
    rl_set_list_possib_func(listPossibleCallback);
#endif
    return restoreRepl;
}

static constexpr const char * promptForType(ReplPromptType promptType)
{
    switch (promptType) {
    case ReplPromptType::ReplPrompt:
        return "nix-repl> ";
    case ReplPromptType::ContinuationPrompt:
        return "          ";
    }
    assert(false);
}

bool ReadlineLikeInteracter::getLine(std::string & input, ReplPromptType promptType)
{
    struct sigaction act, old;
    sigset_t savedSignalMask, set;

    auto setupSignals = [&]() {
        act.sa_handler = sigintHandler;
        sigfillset(&act.sa_mask);
        act.sa_flags = 0;
        if (sigaction(SIGINT, &act, &old))
            throw SysError("installing handler for SIGINT");

        sigemptyset(&set);
        sigaddset(&set, SIGINT);
        if (sigprocmask(SIG_UNBLOCK, &set, &savedSignalMask))
            throw SysError("unblocking SIGINT");
    };
    auto restoreSignals = [&]() {
        if (sigprocmask(SIG_SETMASK, &savedSignalMask, nullptr))
            throw SysError("restoring signals");

        if (sigaction(SIGINT, &old, 0))
            throw SysError("restoring handler for SIGINT");
    };

    setupSignals();
    char * s = readline(promptForType(promptType));
    Finally doFree([&]() { free(s); });
    restoreSignals();

    if (g_signal_received) {
        g_signal_received = 0;
        input.clear();
        return true;
    }

    if (!s)
        return false;
    input += s;
    input += '\n';
    return true;
}

ReadlineLikeInteracter::~ReadlineLikeInteracter()
{
    write_history(historyFile.c_str());
}

};
