#include "paged-logger.hh"

#include <iostream>

namespace nix {

PagedLogger::PagedLogger(std::shared_ptr<Logger> innerLogger)
    : innerLogger(innerLogger)
{
}

PagedLogger::~PagedLogger()
{
    try {
        if (pid != -1) {
            std::cout.flush();
            close(STDOUT_FILENO);
            pid.wait();
        }
    } catch (...) {
        ignoreException();
    }
}

void PagedLogger::startPager() {
    innerLogger->setNonInteractive();
    if (!isatty(STDOUT_FILENO)) return;
    char * pager = getenv("NIX_PAGER");
    if (!pager) pager = getenv("PAGER");
    if (pager && ((string) pager == "" || (string) pager == "cat")) return;

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

    if (dup2(toPager.writeSide.get(), STDOUT_FILENO) == -1)
        throw SysError("dupping stdout");
}

void PagedLogger::writeToStdout(std::string_view s)
{
    if (!pagerStarted)
        startPager();
    innerLogger->writeToStdout(s);
}


void PagedLogger::stop() { return innerLogger->stop(); }
bool PagedLogger::isVerbose() { return innerLogger->isVerbose(); }
void PagedLogger::log(Verbosity lvl, const FormatOrString & fs)
{
    return innerLogger->log(lvl, fs);
}
void PagedLogger::logEI(const ErrorInfo & ei) { return innerLogger->logEI(ei); }
void PagedLogger::warn(const std::string & msg)
{
    return innerLogger->warn(msg);
}
void PagedLogger::startActivity(
    ActivityId act,
    Verbosity lvl,
    ActivityType type,
    const std::string & s,
    const Fields & fields,
    ActivityId parent)
{
    return innerLogger->startActivity(act, lvl, type, s, fields, parent);
}
void PagedLogger::stopActivity(ActivityId act)
{
    return innerLogger->stopActivity(act);
}
void PagedLogger::result(ActivityId act, ResultType type, const Fields & fields)
{
    return innerLogger->result(act, type, fields);
}
void PagedLogger::setNonInteractive()
{
    innerLogger->setNonInteractive();
}
std::optional<char> PagedLogger::ask(std::string_view s)
{
    return innerLogger->ask(s);
}

}
