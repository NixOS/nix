#include "nix/util/signals.hh"
#include "nix/util/util.hh"
#include "nix/util/error.hh"
#include "nix/util/fun.hh"
#include "nix/util/sync.hh"
#include "nix/util/terminal.hh"

#include "unix/signals-private.hh"

#include <thread>

namespace nix {

void Interrupted::anchor() {}

void Cancelled::anchor() {}

std::atomic<bool> unix::_isInterrupted = false;

thread_local std::function<bool()> unix::interruptCheck;

void unix::_interrupted()
{
    /* Block user interrupts while an exception is being handled.
       Throwing an exception while another exception is being handled
       kills the program! */
    if (!std::uncaught_exceptions()) {
        throw Interrupted("interrupted by the user");
    }
}

//////////////////////////////////////////////////////////////////////

/* We keep track of signal callbacks using integer tokens, so we can iterate
   safely without having to lock the data structure while executing arbitrary
   functions.
 */
struct SignalCallbacks
{
    typedef int64_t Token;

    /* We use unique tokens so that we can't accidentally delete the wrong
       handler because of an erroneous double delete. */
    Token nextToken = 0;

    /* Each per-signal map is used as a list, see SignalCallbacks comment. */
    std::map<SignalType, std::map<Token, fun<void()>>> callbacks;
};

InterruptCallback::~InterruptCallback() {}

/* Required to avoid static initialization order fiasco. This allows global
   objects to safely register callbacks. */
static Sync<SignalCallbacks> & getSignalCallbacks()
{
    /* Intentionally leak, according to the Construct On First Use Idiom.
       An alternative is to use the Nifty Counter Idiom, but
       SignalCallbacks' destructor is not very important. */
    static Sync<SignalCallbacks> * _signalCallbacks = new Sync<SignalCallbacks>();
    return *_signalCallbacks;
}

static void triggerSignalCallbacks(SignalType type)
{
    SignalCallbacks::Token i = 0;
    while (true) {
        std::function<void()> callback;
        {
            auto signalCallbacks(getSignalCallbacks().lock());
            auto it = signalCallbacks->callbacks.find(type);
            if (it == signalCallbacks->callbacks.end())
                break;
            auto lb = it->second.lower_bound(i);
            if (lb == it->second.end())
                break;

            callback = lb->second;
            i = lb->first + 1;
        }

        try {
            callback();
        } catch (...) {
            ignoreExceptionInDestructor();
        }
    }
}

static void signalHandlerThread(sigset_t set)
{
    using namespace nix::unix;
    while (true) {
        int signal = 0;
        sigwait(&set, &signal);

        if (signal == SIGINT || signal == SIGTERM || signal == SIGHUP)
            triggerInterrupt();

        else if (signal == SIGWINCH) {
            updateWindowSize();
            triggerSignalCallbacks(SignalType::Winch);
        }

        else if (signal == SIGCONT) {
            /* The terminal may have been resized while we were
               stopped, and we won't have received SIGWINCH for it,
               since the kernel only sends it to the foreground process
               group. */
            updateWindowSize();
            triggerSignalCallbacks(SignalType::Cont);
        }

#ifndef __FreeBSD__
        else if (signal == SIGTSTP) {
            triggerSignalCallbacks(SignalType::Stop);
            /* Since we consumed SIGTSTP via sigwait(), its default
               action (stopping the process) no longer happens, so stop
               ourselves explicitly. SIGSTOP cannot be blocked. On
               resumption, SIGCONT is delivered via sigwait() as
               usual. */
            kill(getpid(), SIGSTOP);
        }
#endif
    }
}

void unix::triggerInterrupt()
{
    _isInterrupted = true;

    triggerSignalCallbacks(SignalType::Int);
}

sigset_t unix::savedSignalMask;
bool unix::savedSignalMaskIsSet = false;

void unix::saveSignalMask()
{
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
    sigaddset(&set, SIGCONT);
#ifndef __FreeBSD__
    /* Not on FreeBSD, where SIGTSTP doubles as NIX_SIG_MULTI_INT and is
       delivered to specific threads using pthread_kill(). */
    sigaddset(&set, SIGTSTP);
#endif
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

namespace {

/* RAII helper to automatically deregister a callback. */
struct InterruptCallbackImpl : InterruptCallback
{
    SignalType type;
    SignalCallbacks::Token token;

    InterruptCallbackImpl(SignalType type, SignalCallbacks::Token token)
        : type(type)
        , token(token)
    {
    }

    InterruptCallbackImpl(InterruptCallbackImpl &&) = delete;
    InterruptCallbackImpl(const InterruptCallbackImpl &) = delete;
    InterruptCallbackImpl & operator=(InterruptCallbackImpl &&) = delete;
    InterruptCallbackImpl & operator=(const InterruptCallbackImpl &) = delete;

    ~InterruptCallbackImpl() override
    {
        auto signalCallbacks(getSignalCallbacks().lock());
        signalCallbacks->callbacks[type].erase(token);
    }
};

} // namespace

std::unique_ptr<InterruptCallback> createSignalCallback(SignalType type, fun<void()> callback)
{
    auto signalCallbacks(getSignalCallbacks().lock());
    auto token = signalCallbacks->nextToken++;
    signalCallbacks->callbacks[type].emplace(token, callback);
    return std::make_unique<InterruptCallbackImpl>(type, token);
}

std::unique_ptr<InterruptCallback> createInterruptCallback(fun<void()> callback)
{
    return createSignalCallback(SignalType::Int, std::move(callback));
}

} // namespace nix
