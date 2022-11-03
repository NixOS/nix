#include "command.hh"
#include "shared.hh"
#include "store-api.hh"
#include "common-args.hh"
#include "names.hh"

#include <regex>

namespace nix {

struct Info
{
    std::string outputName;
};

// name -> version -> store paths
typedef std::map<std::string, std::map<std::string, std::map<StorePath, Info>>> GroupedPaths;

GroupedPaths getClosureInfo(ref<Store> store, const StorePath & toplevel)
{
    StorePathSet closure;
    store->computeFSClosure({toplevel}, closure);

    GroupedPaths groupedPaths;

    for (auto & path : closure) {
        /* Strip the output name. Unfortunately this is ambiguous (we
           can't distinguish between output names like "bin" and
           version suffixes like "unstable"). */
        static std::regex regex("(.*)-([a-z]+|lib32|lib64)");
        std::smatch match;
        std::string name(path.name());
        std::string outputName;
        if (std::regex_match(name, match, regex)) {
            name = match[1];
            outputName = match[2];
        }

        DrvName drvName(name);
        groupedPaths[drvName.name][drvName.version].emplace(path, Info { .outputName = outputName });
    }

    return groupedPaths;
}

std::string showVersions(const std::set<std::string> & versions)
{
    if (versions.empty()) return "∅";
    std::set<std::string> versions2;
    for (auto & version : versions)
        versions2.insert(version.empty() ? "ε" : version);
    return concatStringsSep(", ", versions2);
}

void printClosureDiff(
    ref<Store> store,
    const StorePath & beforePath,
    const StorePath & afterPath,
    std::string_view indent)
{
    auto beforeClosure = getClosureInfo(store, beforePath);
    auto afterClosure = getClosureInfo(store, afterPath);

    std::set<std::string> allNames;
    for (auto & [name, _] : beforeClosure) allNames.insert(name);
    for (auto & [name, _] : afterClosure) allNames.insert(name);

    for (auto & name : allNames) {
        auto & beforeVersions = beforeClosure[name];
        auto & afterVersions = afterClosure[name];

        auto totalSize = [&](const std::map<std::string, std::map<StorePath, Info>> & versions)
        {
            uint64_t sum = 0;
            for (auto & [_, paths] : versions)
                for (auto & [path, _] : paths)
                    sum += store->queryPathInfo(path)->narSize;
            return sum;
        };

        auto beforeSize = totalSize(beforeVersions);
        auto afterSize = totalSize(afterVersions);
        auto sizeDelta = (int64_t) afterSize - (int64_t) beforeSize;
        auto showDelta = std::abs(sizeDelta) >= 8 * 1024;

        std::set<std::string> removed, unchanged;
        for (auto & [version, _] : beforeVersions)
            if (!afterVersions.count(version)) removed.insert(version); else unchanged.insert(version);

        std::set<std::string> added;
        for (auto & [version, _] : afterVersions)
            if (!beforeVersions.count(version)) added.insert(version);

        if (showDelta || !removed.empty() || !added.empty()) {
            std::vector<std::string> items;
            if (!removed.empty() || !added.empty())
                items.push_back(fmt("%s → %s", showVersions(removed), showVersions(added)));
            if (showDelta)
                items.push_back(fmt("%s%+.1f KiB" ANSI_NORMAL, sizeDelta > 0 ? ANSI_RED : ANSI_GREEN, sizeDelta / 1024.0));
            std::cout << fmt("%s%s: %s\n", indent, name, concatStringsSep(", ", items));
        }
    }
}

}

using namespace nix;

struct CmdDiffClosures : SourceExprCommand
{
    std::string _before, _after;

    CmdDiffClosures()
    {
        expectArg("before", &_before);
        expectArg("after", &_after);
    }

    std::string description() override
    {
        return "show what packages and versions were added and removed between two closures";
    }

    std::string doc() override
    {
        return
          #include "diff-closures.md"
          ;
    }

    void run(ref<Store> store) override
    {
        auto before = parseInstallable(store, _before);
        auto beforePath = Installable::toStorePath(getEvalStore(), store, Realise::Outputs, operateOn, before);
        auto after = parseInstallable(store, _after);
        auto afterPath = Installable::toStorePath(getEvalStore(), store, Realise::Outputs, operateOn, after);
        printClosureDiff(store, beforePath, afterPath, "");
    }
};

static auto rCmdDiffClosures = registerCommand2<CmdDiffClosures>({"store", "diff-closures"});
