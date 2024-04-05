#include "signals.hh"
#include "util.hh"
#include "error.hh"
#include "sync.hh"
#include "terminal.hh"

#include <thread>

namespace nix {

using namespace unix;

std::atomic<bool> unix::_isInterrupted = false;

namespace unix {
static thread_local bool interruptThrown = false;
}

thread_local std::function<bool()> unix::interruptCheck;

void setInterruptThrown()
{
    unix::interruptThrown = true;
}

void unix::_interrupted()
{
    /* Block user interrupts while an exception is being handled.
       Throwing an exception while another exception is being handled
       kills the program! */
    if (!interruptThrown && !std::uncaught_exceptions()) {
        interruptThrown = true;
        throw Interrupted("interrupted by the user");
    }
}


//////////////////////////////////////////////////////////////////////


/* We keep track of interrupt callbacks using integer tokens, so we can iterate
   safely without having to lock the data structure while executing arbitrary
   functions.
 */
struct InterruptCallbacks {
    typedef int64_t Token;

    /* We use unique tokens so that we can't accidentally delete the wrong
       handler because of an erroneous double delete. */
    Token nextToken = 0;

    /* Used as a list, see InterruptCallbacks comment. */
    std::map<Token, std::function<void()>> callbacks;
};

static Sync<InterruptCallbacks> _interruptCallbacks;

static void signalHandlerThread(sigset_t set)
{
    while (true) {
        int signal = 0;
        sigwait(&set, &signal);

        if (signal == SIGINT || signal == SIGTERM || signal == SIGHUP)
            triggerInterrupt();

        else if (signal == SIGWINCH) {
            updateWindowSize();
        }
    }
}

void unix::triggerInterrupt()
{
    _isInterrupted = true;

    {
        InterruptCallbacks::Token i = 0;
        while (true) {
            std::function<void()> callback;
            {
                auto interruptCallbacks(_interruptCallbacks.lock());
                auto lb = interruptCallbacks->callbacks.lower_bound(i);
                if (lb == interruptCallbacks->callbacks.end())
                    break;

                callback = lb->second;
                i = lb->first + 1;
            }

            try {
                callback();
            } catch (...) {
                ignoreException();
            }
        }
    }
}


static sigset_t savedSignalMask;
static bool savedSignalMaskIsSet = false;

void unix::setChildSignalMask(sigset_t * sigs)
{
    assert(sigs); // C style function, but think of sigs as a reference

#if _POSIX_C_SOURCE >= 1 || _XOPEN_SOURCE || _POSIX_SOURCE
    sigemptyset(&savedSignalMask);
    // There's no "assign" or "copy" function, so we rely on (math) idempotence
    // of the or operator: a or a = a.
    sigorset(&savedSignalMask, sigs, sigs);
#else
    // Without sigorset, our best bet is to assume that sigset_t is a type that
    // can be assigned directly, such as is the case for a sigset_t defined as
    // an integer type.
    savedSignalMask = *sigs;
#endif

    savedSignalMaskIsSet = true;
}

void unix::saveSignalMask() {
    if (sigprocmask(SIG_BLOCK, nullptr, &savedSignalMask))
        throw SysError("querying signal mask");

    savedSignalMaskIsSet = true;
}

void unix::startSignalHandlerThread()
{
    updateWindowSize();

    saveSignalMask();

    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGPIPE);
    sigaddset(&set, SIGWINCH);
    if (pthread_sigmask(SIG_BLOCK, &set, nullptr))
        throw SysError("blocking signals");

    std::thread(signalHandlerThread, set).detach();
}

void unix::restoreSignals()
{
    // If startSignalHandlerThread wasn't called, that means we're not running
    // in a proper libmain process, but a process that presumably manages its
    // own signal handlers. Such a process should call either
    //  - initNix(), to be a proper libmain process
    //  - startSignalHandlerThread(), to resemble libmain regarding signal
    //    handling only
    //  - saveSignalMask(), for processes that define their own signal handling
    //    thread
    // TODO: Warn about this? Have a default signal mask? The latter depends on
    //       whether we should generally inherit signal masks from the caller.
    //       I don't know what the larger unix ecosystem expects from us here.
    if (!savedSignalMaskIsSet)
        return;

    if (sigprocmask(SIG_SETMASK, &savedSignalMask, nullptr))
        throw SysError("restoring signals");
}


/* RAII helper to automatically deregister a callback. */
struct InterruptCallbackImpl : InterruptCallback
{
    InterruptCallbacks::Token token;
    ~InterruptCallbackImpl() override
    {
        auto interruptCallbacks(_interruptCallbacks.lock());
        interruptCallbacks->callbacks.erase(token);
    }
};

std::unique_ptr<InterruptCallback> createInterruptCallback(std::function<void()> callback)
{
    auto interruptCallbacks(_interruptCallbacks.lock());
    auto token = interruptCallbacks->nextToken++;
    interruptCallbacks->callbacks.emplace(token, callback);

    std::unique_ptr<InterruptCallbackImpl> res {new InterruptCallbackImpl{}};
    res->token = token;

    return std::unique_ptr<InterruptCallback>(res.release());
}

}
