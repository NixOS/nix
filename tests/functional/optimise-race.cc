#ifdef _FILE_OFFSET_BITS
#  undef _FILE_OFFSET_BITS
#endif

#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <csignal>
#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <dlfcn.h>
#include <fcntl.h>
#include <mutex>
#include <string>
#include <thread>
#include <unistd.h>

static auto timeout()
{
    auto value = std::getenv("NIX_TEST_OPTIMISE_TIMEOUT_MS");
    return std::chrono::milliseconds(value ? std::stoi(value) : 10000);
}

static void record(const char * path, const char * value = "")
{
    if (!path)
        return;
    int descriptor = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (descriptor == -1)
        _exit(122);
    if (write(descriptor, value, std::strlen(value)) < 0)
        _exit(122);
    close(descriptor);
}

struct Gate
{
    std::mutex mutex;
    std::condition_variable changed;
    unsigned int arrivals = 0;

    void arrive()
    {
        std::unique_lock lock(mutex);
        ++arrivals;
        changed.notify_all();
        if (!changed.wait_for(lock, timeout(), [&] { return arrivals >= 2; }))
            _exit(120);
    }
};

static void pauseAtLink(const char * source)
{
    if (std::getenv("NIX_TEST_OPTIMISE_CANCEL") && gettid() != getpid())
        return;
    static std::atomic<bool> paused{false};
    auto prefix = std::getenv("NIX_TEST_OPTIMISE_PAUSE");
    if (!prefix || paused.exchange(true))
        return;
    std::string ready = std::string(prefix) + ".ready";
    std::string release = std::string(prefix) + ".release";
    record(ready.c_str(), source);
    auto deadline = std::chrono::steady_clock::now() + timeout();
    while (access(release.c_str(), F_OK) != 0) {
        if (std::chrono::steady_clock::now() >= deadline)
            _exit(121);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}

extern "C" int link(const char * source, const char * destination)
{
    using Link = int (*)(const char *, const char *);
    using Rename = int (*)(const char *, const char *);
    static auto realLink = reinterpret_cast<Link>(dlsym(RTLD_NEXT, "link"));
    static auto realRename = reinterpret_cast<Rename>(dlsym(RTLD_NEXT, "rename"));
    static std::atomic<unsigned int> attempts{0};
    static std::atomic<bool> swapped{false};
    static Gate gate;
    auto target = std::getenv("NIX_TEST_OPTIMISE_LINK");
    bool collision = target && std::strcmp(target, destination) == 0 && attempts.fetch_add(1) < 2;
    if (collision)
        gate.arrive();
    auto swapLink = std::getenv("NIX_TEST_OPTIMISE_SWAP_LINK");
    if (swapLink && std::strcmp(swapLink, source) == 0 && !swapped.exchange(true)) {
        if (realRename(swapLink, std::getenv("NIX_TEST_OPTIMISE_SWAP_BACKUP")) != 0
            || realLink(std::getenv("NIX_TEST_OPTIMISE_SWAP_SOURCE"), swapLink) != 0)
            _exit(123);
        record(std::getenv("NIX_TEST_OPTIMISE_SWAP_MARKER"));
    }
    int result = realLink(source, destination);
    int savedErrno = errno;
    if (collision && result == -1 && savedErrno == EEXIST)
        record(std::getenv("NIX_TEST_OPTIMISE_COLLISION"));
    errno = savedErrno;
    return result;
}

extern "C" int rename(const char * source, const char * destination)
{
    using Rename = int (*)(const char *, const char *);
    static auto realRename = reinterpret_cast<Rename>(dlsym(RTLD_NEXT, "rename"));
    static std::atomic<unsigned int> repairs{0};
    static Gate repairGate;
    auto repair = std::getenv("NIX_TEST_OPTIMISE_REPAIR_LINK");
    if (repair && std::strcmp(repair, destination) == 0 && repairs.fetch_add(1) < 2) {
        repairGate.arrive();
        record(std::getenv("NIX_TEST_OPTIMISE_REPAIR_COLLISION"));
    }
    auto marker = std::getenv("NIX_TEST_OPTIMISE_RENAME_ERROR");
    if (marker && std::strstr(source, "/.tmp-link-")) {
        int descriptor = open(marker, O_WRONLY | O_CREAT | O_EXCL, 0600);
        if (descriptor != -1) {
            close(descriptor);
            errno = EIO;
            return -1;
        }
    }
    if (std::strstr(source, "/.tmp-link-")) {
        auto cancel = std::getenv("NIX_TEST_OPTIMISE_CANCEL");
        if (cancel && gettid() != getpid()) {
            auto deadline = std::chrono::steady_clock::now() + timeout();
            std::string ready = std::string(cancel) + ".ready";
            while (access(ready.c_str(), F_OK) != 0) {
                if (std::chrono::steady_clock::now() >= deadline)
                    _exit(124);
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            }
            std::string error = std::string(cancel) + ".error.ready";
            record(error.c_str(), std::to_string(gettid()).c_str());
            errno = EIO;
            return -1;
        }
        pauseAtLink(source);
    }
    return realRename(source, destination);
}

static int guardedOpen(const char * path, int flags, mode_t mode, const char * symbol)
{
    using Open = int (*)(const char *, int, ...);
    auto realOpen = reinterpret_cast<Open>(dlsym(RTLD_NEXT, symbol));
    auto guard = std::getenv("NIX_TEST_OPTIMISE_READ_GUARD");
    auto marker = std::getenv("NIX_TEST_OPTIMISE_SWAP_MARKER");
    if (guard && marker && (flags & O_ACCMODE) == O_RDONLY && std::strcmp(path, guard) == 0
        && access(marker, F_OK) == 0) {
        errno = EIO;
        return -1;
    }
    return realOpen(path, flags, mode);
}

extern "C" int open(const char * path, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list arguments;
        va_start(arguments, flags);
        mode = va_arg(arguments, mode_t);
        va_end(arguments);
    }
    return guardedOpen(path, flags, mode, "open");
}

extern "C" int open64(const char * path, int flags, ...)
{
    mode_t mode = 0;
    if (flags & O_CREAT) {
        va_list arguments;
        va_start(arguments, flags);
        mode = va_arg(arguments, mode_t);
        va_end(arguments);
    }
    return guardedOpen(path, flags, mode, "open64");
}

extern "C" int __open64_2(const char * path, int flags)
{
    return guardedOpen(path, flags, 0, "open64");
}

extern "C" int __open_2(const char * path, int flags)
{
    return guardedOpen(path, flags, 0, "open");
}

extern "C" int sigwait(const sigset_t * signals, int * signal)
{
    using Sigwait = int (*)(const sigset_t *, int *);
    static auto realSigwait = reinterpret_cast<Sigwait>(dlsym(RTLD_NEXT, "sigwait"));
    static thread_local bool interrupted = false;
    auto prefix = std::getenv("NIX_TEST_OPTIMISE_PAUSE");
    if (prefix && interrupted) {
        std::string acknowledged = std::string(prefix) + ".signal.ready";
        record(acknowledged.c_str(), "interrupted");
        interrupted = false;
    }
    int result = realSigwait(signals, signal);
    if (!result && *signal == SIGINT)
        interrupted = true;
    return result;
}
