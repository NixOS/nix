// nix derivation source-origins — map inputSrc store paths back to
// the original filesystem source paths that were copied into the store
// during evaluation.  This is the missing link between `nix derivation
// show` (which only knows about store paths) and the working-tree
// locations that produced them.

#include "nix/cmd/command.hh"
#include "nix/main/common-args.hh"
#include "nix/store/store-api.hh"
#include "nix/store/derivations.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/eval-settings.hh"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <queue>

using namespace nix;
using json = nlohmann::json;

struct CmdDerivationSourceOrigins : InstallablesCommand, MixPrintJSON
{
    bool recursive = false;

    CmdDerivationSourceOrigins()
    {
        addFlag({
            .longName = "recursive",
            .shortName = 'r',
            .description = "Include the dependencies of the specified derivations.",
            .handler = {&recursive, true},
        });
    }

    std::string description() override
    {
        return "map derivation inputSrcs back to their original source paths";
    }

    std::string doc() override
    {
        return
#include "derivation-source-origins.md"
            ;
    }

    Category category() override
    {
        return catUtility;
    }

    /**
     * Given a SourcePath from the storeToSrc mapping, try to resolve
     * it back to the original filesystem path.  Two strategies:
     *
     *  1. The SourcePath string starts with /nix/store/ (storeFS accessor).
     *     Parse the store path prefix and look it up in
     *     sourceStoreToOriginalPath.
     *
     *  2. The SourcePath's accessor has originalRootPath set (per-input
     *     accessor, e.g. from path: flake inputs).  Combine
     *     originalRootPath with the relative path component.
     */
    std::optional<std::string> resolveOriginalPath(
        ref<Store> store, ref<EvalState> state, const SourcePath & srcPath)
    {
        // Use the canonical path, not to_string() which goes through
        // showPath/resolve and may hide the actual store path.
        auto pathStr = srcPath.path.abs();

        // Strategy 1: store path in the string representation.
        auto storeDir = store->storeDir;
        if (hasPrefix(pathStr, storeDir + "/")) {
            auto afterStore = pathStr.find('/', storeDir.size() + 1);
            std::string storePathStr, relPath;
            if (afterStore == std::string::npos) {
                storePathStr = pathStr;
                relPath = "";
            } else {
                storePathStr = pathStr.substr(0, afterStore);
                relPath = pathStr.substr(afterStore + 1);
            }

            try {
                auto sp = store->parseStorePath(storePathStr);
                auto origPath = state->getOriginalPath(sp);
                if (origPath) {
                    if (relPath.empty())
                        return origPath->string();
                    else
                        return (*origPath / relPath).string();
                }
            } catch (...) {
                // Not a valid store path, fall through.
            }
        }

        // Strategy 2: accessor tagged with originalRootPath by mountInput.
        if (srcPath.accessor->originalRootPath) {
            auto origRoot = *srcPath.accessor->originalRootPath;
            auto relPath = srcPath.path.rel();
            if (relPath.empty() || relPath == ".") {
                return origRoot.string();
            } else {
                return (origRoot / relPath).string();
            }
        }

        return std::nullopt;
    }

    void run(ref<Store> store, Installables && installables) override
    {
        // We need full evaluation (not cached) so that copyPathToStore
        // fires and populates EvalState::storeToSrc. Without this, the
        // eval cache returns cached derivation paths and storeToSrc
        // stays empty.
        evalSettings.useEvalCache = false;

        // Step 1: Evaluate installables to derivation paths.
        // This triggers copyPathToStore for every source reference in the
        // Nix expressions, populating EvalState::storeToSrc.
        auto drvPaths = Installable::toDerivations(store, installables, true);

        if (recursive) {
            StorePathSet closure;
            store->computeFSClosure(drvPaths, closure);
            drvPaths = std::move(closure);
        }

        // Step 2: Grab the full store→source mapping that was built during
        // evaluation, plus the sourceStoreToOriginalPath mapping from mountInput.
        auto state = getEvalState();
        auto origins = state->getSourceOrigins();

        // Step 3: For each derivation, read its inputSrcs and look up
        // source origins.
        json jsonRoot = json::object();

        for (auto & drvPath : drvPaths) {
            if (!drvPath.isDerivation())
                continue;

            auto drv = store->readDerivation(drvPath);

            json inputSrcsJson = json::object();
            for (auto & inputSrc : drv.inputSrcs) {
                json entry = json::object();
                entry["storePath"] = store->printStorePath(inputSrc);

                // First, check if recordPathOrigin directly resolved this
                // to an original filesystem path (handles cleanSourceWith,
                // builtins.path, and all path: inputs).
                auto origPath = state->getOriginalPath(inputSrc);
                if (origPath) {
                    entry["sourcePath"] = origPath->string();
                } else {
                    // Fall back to the storeToSrc reverse mapping.
                    auto it = origins.find(inputSrc);
                    if (it != origins.end()) {
                        auto & srcPath = it->second;
                        auto resolved = resolveOriginalPath(store, state, srcPath);
                        if (resolved) {
                            entry["sourcePath"] = *resolved;
                        } else {
                            auto physPath = srcPath.getPhysicalPath();
                            if (physPath) {
                                entry["sourcePath"] = physPath->string();
                            } else {
                                entry["sourcePath"] = srcPath.to_string();
                            }
                        }
                    } else {
                        entry["sourcePath"] = nullptr;
                    }
                }

                // When the sourcePath is a directory (e.g. from cleanSourceWith
                // with a broad src like ../../../.), enumerate the store path
                // contents to get file-level precision.  The store path IS the
                // filtered result, so its contents are exactly what passed the
                // filter.  Map each file back to its original source location.
                auto sourcePathStr = entry.value("sourcePath", "");
                if (!sourcePathStr.empty() && sourcePathStr != "null") {
                    auto storePathStr = store->printStorePath(inputSrc);
                    try {
                        namespace fs = std::filesystem;
                        fs::path sp(storePathStr);
                        if (fs::exists(sp) && fs::is_directory(sp)) {
                            fs::path sourceRoot(sourcePathStr);
                            json filesArr = json::array();
                            // BFS to enumerate all files in the store path
                            std::queue<fs::path> dirs;
                            dirs.push(sp);
                            while (!dirs.empty()) {
                                auto dir = dirs.front();
                                dirs.pop();
                                for (auto & de : fs::directory_iterator(dir)) {
                                    if (de.is_directory()) {
                                        dirs.push(de.path());
                                    } else {
                                        // Get path relative to store root
                                        auto relPath = fs::relative(de.path(), sp);
                                        // Map back to original source location
                                        auto origFile = sourceRoot / relPath;
                                        filesArr.push_back(origFile.string());
                                    }
                                }
                            }
                            if (!filesArr.empty()) {
                                entry["sourceFiles"] = filesArr;
                            }
                        }
                    } catch (...) {
                        // Store path not available (not built yet) — skip
                    }
                }

                inputSrcsJson[store->printStorePath(inputSrc)] = entry;
            }

            json drvJson = json::object();
            drvJson["drvPath"] = store->printStorePath(drvPath);
            drvJson["name"] = drv.name;
            drvJson["inputSrcs"] = inputSrcsJson;
            jsonRoot[store->printStorePath(drvPath)] = drvJson;
        }

        printJSON(jsonRoot);
    }
};

static auto rCmdDerivationSourceOrigins =
    registerCommand2<CmdDerivationSourceOrigins>({"derivation", "source-origins"});
