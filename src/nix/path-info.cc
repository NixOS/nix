#include "nix/cmd/command.hh"
#include "nix/main/shared.hh"
#include "nix/store/store-api.hh"
#include "nix/main/common-args.hh"
#include "nix/store/nar-info.hh"

#include <algorithm>
#include <array>

#include <nlohmann/json.hpp>

#include "nix/util/strings.hh"

using namespace nix;
using nlohmann::json;

/**
 * @return the total size of a set of store objects (specified by path),
 * that is, the sum of the size of the NAR serialisation of each object
 * in the set.
 */
static uint64_t getStoreObjectsTotalSize(Store & store, const StorePathSet & closure)
{
    uint64_t totalNarSize = 0;
    for (auto & p : closure) {
        totalNarSize += store.queryPathInfo(p)->narSize;
    }
    return totalNarSize;
}

/**
 * Write a JSON representation of store object metadata, such as the
 * hash and the references.
 *
 * @param showClosureSize If true, the closure size of each path is
 * included.
 */
static json pathInfoToJSON(Store & store, const StorePathSet & storePaths, bool showClosureSize)
{
    json::object_t jsonAllObjects = json::object();

    for (auto & storePath : storePaths) {
        json jsonObject;
        auto printedStorePath = store.printStorePath(storePath);

        try {
            auto info = store.queryPathInfo(storePath);

            // `storePath` has the representation `<hash>-x` rather than
            // `<hash>-<name>` in case of binary-cache stores & `--all` because we don't
            // know the name yet until we've read the NAR info.
            printedStorePath = store.printStorePath(info->path);

            jsonObject = info->toJSON(&store, true);

            if (showClosureSize) {
                StorePathSet closure;
                store.computeFSClosure(storePath, closure, false, false);

                jsonObject["closureSize"] = getStoreObjectsTotalSize(store, closure);

                if (dynamic_cast<const NarInfo *>(&*info)) {
                    uint64_t totalDownloadSize = 0;
                    for (auto & p : closure) {
                        auto depInfo = store.queryPathInfo(p);
                        if (auto * depNarInfo = dynamic_cast<const NarInfo *>(&*depInfo))
                            totalDownloadSize += depNarInfo->fileSize;
                        else
                            throw Error(
                                "Missing .narinfo for dep %s of %s",
                                store.printStorePath(p),
                                store.printStorePath(storePath));
                    }
                    jsonObject["closureDownloadSize"] = totalDownloadSize;
                }
            }

        } catch (InvalidPath &) {
            jsonObject = nullptr;
        }

        jsonAllObjects[printedStorePath] = std::move(jsonObject);
    }
    return jsonAllObjects;
}

struct CmdPathInfo : StorePathsCommand, MixJSON
{
    bool showSize = false;
    bool showClosureSize = false;
    bool humanReadable = false;
    bool showSigs = false;

    CmdPathInfo()
    {
        addFlag({
            .longName = "size",
            .shortName = 's',
            .description = "Print the size of the NAR serialisation of each path.",
            .handler = {&showSize, true},
        });

        addFlag({
            .longName = "closure-size",
            .shortName = 'S',
            .description = "Print the sum of the sizes of the NAR serialisations of the closure of each path.",
            .handler = {&showClosureSize, true},
        });

        addFlag({
            .longName = "human-readable",
            .shortName = 'h',
            .description = "With `-s` and `-S`, print sizes in a human-friendly format such as `5.67G`.",
            .handler = {&humanReadable, true},
        });

        addFlag({
            .longName = "sigs",
            .description = "Show signatures.",
            .handler = {&showSigs, true},
        });
    }

    std::string description() override
    {
        return "query information about store paths";
    }

    std::string doc() override
    {
        return
#include "path-info.md"
            ;
    }

    Category category() override
    {
        return catSecondary;
    }

    void printSize(std::ostream & str, uint64_t value)
    {
        if (humanReadable)
            str << fmt("\t%s", renderSize((int64_t) value, true));
        else
            str << fmt("\t%11d", value);
    }

    void run(ref<Store> store, StorePaths && storePaths) override
    {
        size_t pathLen = 0;
        for (auto & storePath : storePaths)
            pathLen = std::max(pathLen, store->printStorePath(storePath).size());

        if (json) {
            printJSON(pathInfoToJSON(
                *store,
                // FIXME: preserve order?
                StorePathSet(storePaths.begin(), storePaths.end()),
                showClosureSize));
        }

        else {

            for (auto & storePath : storePaths) {
                auto info = store->queryPathInfo(storePath);
                auto storePathS = store->printStorePath(info->path);

                std::ostringstream str;

                str << storePathS;

                if (showSize || showClosureSize || showSigs)
                    str << std::string(std::max(0, (int) pathLen - (int) storePathS.size()), ' ');

                if (showSize)
                    printSize(str, info->narSize);

                if (showClosureSize) {
                    StorePathSet closure;
                    store->computeFSClosure(storePath, closure, false, false);
                    printSize(str, getStoreObjectsTotalSize(*store, closure));
                }

                if (showSigs) {
                    str << '\t';
                    Strings ss;
                    if (info->ultimate)
                        ss.push_back("ultimate");
                    if (info->ca)
                        ss.push_back("ca:" + renderContentAddress(*info->ca));
                    for (auto & sig : info->sigs)
                        ss.push_back(sig);
                    str << concatStringsSep(" ", ss);
                }

                logger->cout(str.str());
            }
        }
    }
};

static auto rCmdPathInfo = registerCommand<CmdPathInfo>("path-info");
