#include "nix/cmd/get-build-log.hh"
#include "nix/store/log-store.hh"
#include "nix/store/store-open.hh"

namespace nix {

std::string fetchBuildLog(Settings & settings, ref<Store> store, const StorePath & path, std::string_view what)
{
    auto subs = getDefaultSubstituters(settings);

    subs.push_front(store);

    for (auto & sub : subs) {
        auto * logSubP = dynamic_cast<LogStore *>(&*sub);
        if (!logSubP) {
            printInfo("Skipped '%s' which does not support retrieving build logs", sub->config.getHumanReadableURI());
            continue;
        }
        auto & logSub = *logSubP;

        auto log = logSub.getBuildLog(path);
        if (!log)
            continue;
        printInfo("got build log for '%s' from '%s'", what, logSub.config.getHumanReadableURI());
        return *log;
    }

    throw Error("build log of '%s' is not available", what);
}

} // namespace nix
