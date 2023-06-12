#include "paged-logger.hh"

#include <iostream>

namespace nix {

PagedLogger::PagedLogger(Logger * previousLogger)
    : innerLogger(previousLogger)
{
}

PagedLogger::~PagedLogger()
{
    try {
        if (pid != -1) {
            std::cout.flush();
            dup2(stdout, STDOUT_FILENO);
            pid.wait();
        }
    } catch (...) {
        ignoreException();
    }
}

void PagedLogger::startPager() {
    if (pagerStarted) {
        return;
    }
    innerLogger = makeSimpleLogger(false);
    pagerStarted = true;
    if (!isatty(STDOUT_FILENO)) return;
    char * pager = getenv("NIX_PAGER");
    if (!pager) pager = getenv("PAGER");
    if (pager && ((std::string) pager == "" || (std::string) pager == "cat")) return;

    Pipe toPager;
    toPager.create();

    pid = startProcess([&]() {
        if (dup2(toPager.readSide.get(), STDIN_FILENO) == -1)
            throw SysError("dupping stdin");
        if (!getenv("LESS"))
            setenv("LESS", "FRSXMK", 1);
        restoreProcessContext();
        if (pager)
            execl("/bin/sh", "sh", "-c", pager, nullptr);
        execlp("pager", "pager", nullptr);
        execlp("less", "less", nullptr);
        execlp("more", "more", nullptr);
        throw SysError("executing '%1%'", pager);
    });

    pid.setKillSignal(SIGINT);

    pid.setKillSignal(SIGINT);
    stdout = fcntl(STDOUT_FILENO, F_DUPFD_CLOEXEC, 0);
    if (dup2(toPager.writeSide.get(), STDOUT_FILENO) == -1)
        throw SysError("dupping stdout");
}

void PagedLogger::writeToStdout(std::string_view s)
{
    startPager();
    innerLogger->writeToStdout(s);
}


void PagedLogger::stop() { return innerLogger->stop(); }
bool PagedLogger::isVerbose() { return false; }
void PagedLogger::log(Verbosity lvl, const std::string_view s)
{ innerLogger->log(lvl, s); }
void PagedLogger::logEI(const ErrorInfo & ei) { innerLogger->logEI(ei); }
void PagedLogger::warn(const std::string & msg)
{ innerLogger->warn(msg);  }
void PagedLogger::startActivity(
    ActivityId act,
    Verbosity lvl,
    ActivityType type,
    const std::string & s,
    const Fields & fields,
    ActivityId parent)
{ innerLogger->startActivity(act, lvl, type, s, fields, parent); }
void PagedLogger::stopActivity(ActivityId act)
{ innerLogger->stopActivity(act); }
void PagedLogger::result(ActivityId act, ResultType type, const Fields & fields)
{ innerLogger->result(act, type, fields); }
std::optional<char> PagedLogger::ask(std::string_view s)
{ return innerLogger->ask(s); }

}
