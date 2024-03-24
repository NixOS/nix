#include "command.hh"
#include "shared.hh"
#include "store-api.hh"
#include "common-args.hh"
#include "nar-info.hh"

#include <algorithm>
#include <array>

#include <nlohmann/json.hpp>

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
static json pathInfoToJSON(
    Store & store,
    const StorePathSet & storePaths,
    bool showClosureSize)
{
    json::object_t jsonAllObjects = json::object();

    for (auto & storePath : storePaths) {
        json jsonObject;

        try {
            auto info = store.queryPathInfo(storePath);

            jsonObject = info->toJSON(store, true, HashFormat::SRI);

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
                            throw Error("Missing .narinfo for dep %s of %s",
                                store.printStorePath(p),
                                store.printStorePath(storePath));
                    }
                    jsonObject["closureDownloadSize"] = totalDownloadSize;
                }
            }

        } catch (InvalidPath &) {
            jsonObject = nullptr;
        }

        jsonAllObjects[store.printStorePath(storePath)] = std::move(jsonObject);
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

    Category category() override { return catSecondary; }

    void printSize(uint64_t value)
    {
        if (!humanReadable) {
            std::cout << fmt("\t%11d", value);
            return;
        }

        static const std::array<char, 9> idents{{
            ' ', 'K', 'M', 'G', 'T', 'P', 'E', 'Z', 'Y'
        }};
        size_t power = 0;
        double res = value;
        while (res > 1024 && power < idents.size()) {
            ++power;
            res /= 1024;
        }
        std::cout << fmt("\t%6.1f%c", res, idents.at(power));
    }

    void run(ref<Store> store, StorePaths && storePaths) override
    {
        size_t pathLen = 0;
        for (auto & storePath : storePaths)
            pathLen = std::max(pathLen, store->printStorePath(storePath).size());

        if (json) {
            std::cout << pathInfoToJSON(
                *store,
                // FIXME: preserve order?
                StorePathSet(storePaths.begin(), storePaths.end()),
                showClosureSize).dump();
        }

        else {

            for (auto & storePath : storePaths) {
                auto info = store->queryPathInfo(storePath);
                auto storePathS = store->printStorePath(info->path);

                std::cout << storePathS;

                if (showSize || showClosureSize || showSigs)
                    std::cout << std::string(std::max(0, (int) pathLen - (int) storePathS.size()), ' ');

                if (showSize)
                    printSize(info->narSize);

                if (showClosureSize) {
                    StorePathSet closure;
                    store->computeFSClosure(storePath, closure, false, false);
                    printSize(getStoreObjectsTotalSize(*store, closure));
                }

                if (showSigs) {
                    std::cout << '\t';
                    Strings ss;
                    if (info->ultimate) ss.push_back("ultimate");
                    if (info->ca) ss.push_back("ca:" + renderContentAddress(*info->ca));
                    for (auto & sig : info->sigs) ss.push_back(sig);
                    std::cout << concatStringsSep(" ", ss);
                }

                std::cout << std::endl;
            }

        }
    }
};

static auto rCmdPathInfo = registerCommand<CmdPathInfo>("path-info");
