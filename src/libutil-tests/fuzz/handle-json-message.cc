#include "nix/util/logging.hh"

namespace {

class BlackholeLogger : public nix::Logger
{
    void log(nix::Verbosity lvl, std::string_view s) noexcept override {}

    void logEI(const nix::ErrorInfo & ei) noexcept override {}
};

} // namespace

extern "C" int LLVMFuzzerInitialize(int * argc, char *** argv)
{
    static BlackholeLogger blackhole = {};
    nix::logger = &blackhole;
    return 0;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t * data, size_t size)
{
    using namespace nix;

    if (size < 1)
        return -1;

    std::string_view body(reinterpret_cast<const char *>(data), size);

    Activity act(*logger, lvlVomit, actUnknown, "");
    std::map<ActivityId, Activity> activities;

    size_t pos = 0;
    while (pos < body.size()) {
        auto end = body.find('\n', pos);

        if (end == std::string_view::npos)
            end = body.size();

        auto line = body.substr(pos, end - pos);
        pos = end + 1;

        if (line.empty())
            continue;

        try {
            handleJSONLogMessage(std::string(line), act, activities, "dummy fuzzing source", /*trusted=*/true);
        } catch (std::exception & e) {
        }

        if (activities.size() > 128)
            break;
    }

    return 0;
}
