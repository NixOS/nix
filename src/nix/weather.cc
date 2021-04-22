#include "command.hh"
#include "store-api.hh"
#include "progress-bar.hh"
#include "shared.hh"
#include "eval-cache.hh"
#include "attr-path.hh"
#include "nar-info-disk-cache.hh"

#include <queue>

using namespace nix;

struct CmdWeather : InstallablesCommand
{
    bool noClosure = false;

    CmdWeather()
    {
        addFlag({
            .longName = "no-closure",
            .description = "Do not compute the closure of the paths.",
            .handler = {&this->noClosure, true},
        });
    }

    std::string description() override
    {
        return "show why a package has another package in its closure";
    }

    std::string doc() override
    {
        return
          #include "weather.md"
          ;
    }

    Category category() override { return catSecondary; }

    Strings getDefaultFlakeAttrPaths() override
    {
        return {
            "packages." + settings.thisSystem.get() + ".",
            "legacyPackages." + settings.thisSystem.get() + "."
        };
    }

    void run(ref<Store> store) override
    {
        StorePathSet drvPaths;

        auto state = getEvalState();

        // from search.cc
        std::function<void(eval_cache::AttrCursor & cursor, const std::vector<Symbol> & attrPath, bool initialRecurse)> visit;
        visit = [&](eval_cache::AttrCursor & cursor, const std::vector<Symbol> & attrPath, bool initialRecurse)
        {
            Activity act(*logger, lvlInfo, actUnknown,
                fmt("evaluating '%s'", concatStringsSep(".", attrPath)));
            try {
                auto recurse = [&]()
                {
                    for (const auto & attr : cursor.getAttrs()) {
                        auto cursor2 = cursor.getAttr(attr);
                        auto attrPath2(attrPath);
                        attrPath2.push_back(attr);
                        visit(*cursor2, attrPath2, false);
                    }
                };

                if (cursor.isDerivation()) {
                    auto drvPath = cursor.forceDerivation();
                    drvPaths.insert(drvPath);
                }

                else if (
                    attrPath.size() == 0
                    || (attrPath[0] == "legacyPackages" && attrPath.size() <= 2)
                    || (attrPath[0] == "packages" && attrPath.size() <= 2))
                    recurse();

                else if (initialRecurse)
                    recurse();

                else if (attrPath[0] == "legacyPackages" && attrPath.size() > 2) {
                    auto attr = cursor.maybeGetAttr(state->sRecurseForDerivations);
                    if (attr && attr->getBool())
                        recurse();
                }

            } catch (EvalError & e) {
                if (!(attrPath.size() > 0 && attrPath[0] == "legacyPackages"))
                    throw;
            }
        };

        for (auto & installable : installables) {
            if (auto installable2 = std::dynamic_pointer_cast<InstallableFlake>(installable)) {
                for (auto & [cursor, prefix] : installable2->getCursors(*state)) {
                    visit(*cursor, parseAttrPath(*state, prefix), true);
                }
            } else {
                auto drvPaths_ = toDerivations(store, {installable}, true);
                drvPaths.insert(drvPaths_.begin(), drvPaths_.end());
            }
        }

        if (drvPaths.size() == 0)
            throw Error("no derivations found!");

        StorePathSet closure;
        if (!noClosure) {
            Activity act(*logger, lvlInfo, actUnknown, "computing closure");
            store->computeFSClosure(std::move(drvPaths), closure); // this could be huge ; should we chunk it up?
        } else
            closure = std::move(drvPaths);

        StorePathSet outPaths;
        for (auto & path : closure) {
            Activity act(*logger, lvlInfo, actUnknown, fmt("querying '%s' output paths", store->printStorePath(path)));
            if (isDerivation(store->printStorePath(path))) {
                auto outputs = store->queryDerivationOutputs(path);
                outPaths.insert(outputs.begin(), outputs.end());
            }
            // TODO: what to do with non-derivations?
        }

        StorePathSet totalValidPaths;
        ssize_t totalPathsFound = 0;
        uint64_t totalNarSize = 0;
        std::optional<uint64_t> totalDownloadSize = 0;

        auto subs = getDefaultSubstituters();
        for (auto & sub : subs) {
            StorePathSet validPaths = sub->queryValidPaths(outPaths);

            ssize_t pathsFound = 0;
            uint64_t narSize = 0;
            std::optional<uint64_t> downloadSize = 0;

            for (auto & path : validPaths) {
                auto info = sub->queryPathInfo(path);
                auto narInfo = std::dynamic_pointer_cast<const NarInfo>(std::shared_ptr<const ValidPathInfo>(info));

                pathsFound++;
                if (downloadSize && narInfo)
                    *downloadSize += narInfo->fileSize;
                else
                    downloadSize = std::nullopt;
                narSize += info->narSize;

                if (totalValidPaths.count(path) == 0) {
                    totalValidPaths.insert(path);
                    totalPathsFound++;
                    if (totalDownloadSize && narInfo)
                        *totalDownloadSize += narInfo->fileSize;
                    else
                        totalDownloadSize = std::nullopt;
                    totalNarSize += info->narSize;
                }
            }

            logger->cout("Substituter %s", sub->getUri());
            logger->cout("  %6.1f%% of paths have substitutes available (%s of %s)", (float) 100 * pathsFound / outPaths.size(), pathsFound, outPaths.size());
            if (downloadSize)
                logger->cout("  %s downloaded (compressed)", formatSize(*downloadSize));
            logger->cout("  %s downloaded (uncompressed)", formatSize(narSize));
            logger->cout("");
        }

        if (subs.size() > 1) {
            logger->cout("Total");
            logger->cout("  %6.0f%% of paths have substitutes available (%s of %s)", (float) 100 * totalPathsFound / outPaths.size(), totalPathsFound, outPaths.size());
            if (totalDownloadSize)
                logger->cout("  %s downloaded (compressed)", formatSize(*totalDownloadSize));
            logger->cout("  %s downloaded (uncompressed)", formatSize(totalNarSize));
        }
    }
};

static auto rCmdWeather = registerCommand<CmdWeather>("weather");
