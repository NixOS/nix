#include "logging.hh"
#include "util.hh"

namespace nix {

class PagedLogger : public Logger {
    std::shared_ptr<Logger> innerLogger;
    bool pagerStarted;
    Pid pid;

    void startPager();

public:
    PagedLogger(std::shared_ptr<Logger> innerLogger);

    ~PagedLogger();

    void writeToStdout(std::string_view s) override;

    // All these methods are just forwarded to the inner logger

    void stop() override;
    bool isVerbose() override;
    void log(Verbosity lvl, const FormatOrString &fs) override;
    void logEI(const ErrorInfo &ei) override;
    void warn(const std::string &msg) override;
    void startActivity(ActivityId act, Verbosity lvl, ActivityType type, const std::string &s, const Fields &fields, ActivityId parent) override;
    void stopActivity(ActivityId act) override;
    void result(ActivityId act, ResultType type, const Fields &fields) override;
    void setNonInteractive() override;
    std::optional<char> ask(std::string_view s) override;
};

}
