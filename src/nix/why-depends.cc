#include "nix/cmd/command.hh"
#include "nix/store/store-api.hh"
#include "nix/store/path-references.hh"
#include "nix/store/dependency-graph-impl.hh"
#include "nix/util/source-accessor.hh"
#include "nix/main/shared.hh"

#include <ranges>

using namespace nix;

static std::string hilite(const std::string & s, size_t pos, size_t len, const std::string & colour = ANSI_RED)
{
    return std::string(s, 0, pos) + colour + std::string(s, pos, len) + ANSI_NORMAL + std::string(s, pos + len);
}

static std::string filterPrintable(const std::string & s)
{
    std::string res;
    for (char c : s)
        res += isprint(c) ? c : '.';
    return res;
}

/**
 * Find and format hash references in scanned files with context.
 *
 * @param accessor Source accessor for the store path
 * @param refPaths Store paths to search for
 * @param dependencyPathHash Hash of the dependency (for coloring)
 * @return Map from hash string to list of formatted hit strings showing context
 */
static std::map<std::string, Strings>
findHashContexts(SourceAccessor & accessor, const StorePathSet & refPaths, std::string_view dependencyPathHash)
{
    std::map<std::string, Strings> hits;

    auto getColour = [&](const std::string & hash) { return hash == dependencyPathHash ? ANSI_GREEN : ANSI_BLUE; };

    scanForReferencesDeep(accessor, CanonPath::root, refPaths, [&](const FileRefScanResult & result) {
        std::string p2 =
            result.filePath.isRoot() ? std::string(result.filePath.abs()) : std::string(result.filePath.rel());
        auto st = accessor.lstat(result.filePath);

        if (st.type == SourceAccessor::Type::tRegular) {
            auto contents = accessor.readFile(result.filePath);

            // For each reference found in this file, extract context
            for (const auto & foundRef : result.foundRefs) {
                std::string hash(foundRef.hashPart());
                auto pos = contents.find(hash);
                if (pos != std::string::npos) {
                    size_t margin = 32;
                    auto pos2 = pos >= margin ? pos - margin : 0;
                    hits[hash].emplace_back(
                        fmt("%s: …%s…",
                            p2,
                            hilite(
                                filterPrintable(std::string(contents, pos2, pos - pos2 + hash.size() + margin)),
                                pos - pos2,
                                StorePath::HashLen,
                                getColour(hash))));
                }
            }
        } else if (st.type == SourceAccessor::Type::tSymlink) {
            auto target = accessor.readLink(result.filePath);

            // For each reference found in this symlink, show it
            for (const auto & foundRef : result.foundRefs) {
                std::string hash(foundRef.hashPart());
                auto pos = target.find(hash);
                if (pos != std::string::npos)
                    hits[hash].emplace_back(
                        fmt("%s -> %s", p2, hilite(target, pos, StorePath::HashLen, getColour(hash))));
            }
        }
    });

    return hits;
}

struct CmdWhyDepends : SourceExprCommand, MixOperateOnOptions
{
    std::string _package, _dependency;
    bool all = false;
    bool precise = false;

    CmdWhyDepends()
    {
        expectArgs({
            .label = "package",
            .handler = {&_package},
            .completer = getCompleteInstallable(),
        });

        expectArgs({
            .label = "dependency",
            .handler = {&_dependency},
            .completer = getCompleteInstallable(),
        });

        addFlag({
            .longName = "all",
            .shortName = 'a',
            .description =
                "Show all edges in the dependency graph leading from *package* to *dependency*, rather than just a shortest path.",
            .handler = {&all, true},
        });

        addFlag({
            .longName = "precise",
            .description =
                "For each edge in the dependency graph, show the files in the parent that cause the dependency.",
            .handler = {&precise, true},
        });
    }

    std::string description() override
    {
        return "show why a package has another package in its closure";
    }

    std::string doc() override
    {
        return
#include "why-depends.md"
            ;
    }

    Category category() override
    {
        return catSecondary;
    }

    void run(ref<Store> store) override
    {
        auto package = parseInstallable(store, _package);
        auto packagePath = Installable::toStorePath(getEvalStore(), store, Realise::Outputs, operateOn, package);

        /* We don't need to build `dependency`. We try to get the store
         * path if it's already known, and if not, then it's not a dependency.
         *
         * Why? If `package` does depends on `dependency`, then getting the
         * store path of `package` above necessitated having the store path
         * of `dependency`. The contrapositive is, if the store path of
         * `dependency` is not already known at this point (i.e. it's a CA
         * derivation which hasn't been built), then `package` did not need it
         * to build.
         */
        auto dependency = parseInstallable(store, _dependency);
        auto optDependencyPath = [&]() -> std::optional<StorePath> {
            try {
                return {Installable::toStorePath(getEvalStore(), store, Realise::Derivation, operateOn, dependency)};
            } catch (MissingRealisation &) {
                return std::nullopt;
            }
        }();

        StorePathSet closure;
        store->computeFSClosure({packagePath}, closure, false, false);

        if (!optDependencyPath.has_value() || !closure.count(*optDependencyPath)) {
            printError("'%s' does not depend on '%s'", package->what(), dependency->what());
            return;
        }

        auto dependencyPath = *optDependencyPath;
        auto dependencyPathHash = dependencyPath.hashPart();

        // Build dependency graph from closure using store metadata
        StorePathGraph depGraph(*store, closure);

        /* Print the subgraph of nodes that have 'dependency' in their
           closure (i.e., that have a non-infinite distance to
           'dependency'). Print every edge on a path between `package`
           and `dependency`. */

        RunPager pager;

        if (!precise) {
            logger->cout("%s", store->printStorePath(packagePath));
        }

        std::set<StorePath> visited;
        std::vector<std::string> padStack = {""};

        depGraph.dfsFromTarget(
            packagePath,
            dependencyPath,
            // Visit node callback
            [&](const StorePath & node, size_t depth) -> bool {
                std::string currentPad = padStack[depth];

                if (precise) {
                    logger->cout(
                        "%s%s%s%s" ANSI_NORMAL,
                        currentPad,
                        visited.contains(node) ? "\e[38;5;244m" : "",
                        currentPad != "" ? "→ " : "",
                        store->printStorePath(node));
                }

                if (visited.contains(node)) {
                    return false; // Skip subtree
                }
                if (precise) {
                    visited.insert(node);
                }

                return true; // Continue with this node's children
            },
            // Visit edge callback
            [&](const StorePath & from, const StorePath & to, bool isLast, size_t depth) {
                std::string tailPad = padStack[depth];

                // In non-all mode, we only traverse one path, so everything is "last"
                bool effectivelyLast = !all || isLast;

                if (precise) {
                    auto accessor = store->requireStoreObjectAccessor(from);
                    auto hits = findHashContexts(*accessor, {to}, dependencyPathHash);
                    std::string hash(to.hashPart());
                    auto & hashHits = hits[hash];

                    for (auto & hit : hashHits) {
                        bool first = hit == *hashHits.begin();
                        logger->cout(
                            "%s%s%s",
                            tailPad,
                            (first ? (effectivelyLast ? treeLast : treeConn) : (effectivelyLast ? treeNull : treeLine)),
                            hit);
                        if (!all)
                            break;
                    }
                } else {
                    std::string currentPad = padStack[depth];
                    logger->cout(
                        "%s%s%s%s" ANSI_NORMAL,
                        currentPad,
                        visited.contains(to) ? "\e[38;5;244m" : "",
                        effectivelyLast ? treeLast : treeConn,
                        store->printStorePath(to));
                    visited.insert(from);
                }

                // Update padding for next level
                if (padStack.size() == depth + 1) {
                    padStack.push_back(tailPad + (effectivelyLast ? treeNull : treeLine));
                } else {
                    padStack[depth + 1] = tailPad + (effectivelyLast ? treeNull : treeLine);
                }
            },
            // Stop condition
            [&](const StorePath & node) { return node == dependencyPath && !all && packagePath != dependencyPath; });
    }
};

static auto rCmdWhyDepends = registerCommand<CmdWhyDepends>("why-depends");
