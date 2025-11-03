#include "nix/cmd/command.hh"
#include "nix/store/store-api.hh"
#include "nix/store/path-references.hh"
#include "nix/util/source-accessor.hh"
#include "nix/main/shared.hh"

#include <queue>

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

        auto const inf = std::numeric_limits<size_t>::max();

        struct Node
        {
            StorePath path;
            StorePathSet refs;
            StorePathSet rrefs;
            size_t dist = inf;
            Node * prev = nullptr;
            bool queued = false;
            bool visited = false;
        };

        std::map<StorePath, Node> graph;

        for (auto & path : closure)
            graph.emplace(
                path,
                Node{
                    .path = path,
                    .refs = store->queryPathInfo(path)->references,
                    .dist = path == dependencyPath ? 0 : inf});

        // Transpose the graph.
        for (auto & node : graph)
            for (auto & ref : node.second.refs)
                graph.find(ref)->second.rrefs.insert(node.first);

        /* Run Dijkstra's shortest path algorithm to get the distance
           of every path in the closure to 'dependency'. */
        std::priority_queue<Node *> queue;

        queue.push(&graph.at(dependencyPath));

        while (!queue.empty()) {
            auto & node = *queue.top();
            queue.pop();

            for (auto & rref : node.rrefs) {
                auto & node2 = graph.at(rref);
                auto dist = node.dist + 1;
                if (dist < node2.dist) {
                    node2.dist = dist;
                    node2.prev = &node;
                    if (!node2.queued) {
                        node2.queued = true;
                        queue.push(&node2);
                    }
                }
            }
        }

        /* Print the subgraph of nodes that have 'dependency' in their
           closure (i.e., that have a non-infinite distance to
           'dependency'). Print every edge on a path between `package`
           and `dependency`. */
        std::function<void(Node &, const std::string &, const std::string &)> printNode;

        struct BailOut
        {};

        printNode = [&](Node & node, const std::string & firstPad, const std::string & tailPad) {
            assert(node.dist != inf);
            if (precise) {
                logger->cout(
                    "%s%s%s%s" ANSI_NORMAL,
                    firstPad,
                    node.visited ? "\e[38;5;244m" : "",
                    firstPad != "" ? "→ " : "",
                    store->printStorePath(node.path));
            }

            if (node.path == dependencyPath && !all && packagePath != dependencyPath)
                throw BailOut();

            if (node.visited)
                return;
            if (precise)
                node.visited = true;

            /* Sort the references by distance to `dependency` to
               ensure that the shortest path is printed first. */
            std::multimap<size_t, Node *> refs;
            StorePathSet refPaths;

            for (auto & ref : node.refs) {
                if (ref == node.path && packagePath != dependencyPath)
                    continue;
                auto & node2 = graph.at(ref);
                if (node2.dist == inf)
                    continue;
                refs.emplace(node2.dist, &node2);
                refPaths.insert(node2.path);
            }

            /* For each reference, find the files and symlinks that
               contain the reference. */
            std::map<std::string, Strings> hits;

            auto accessor = store->requireStoreObjectAccessor(node.path);

            auto getColour = [&](const std::string & hash) {
                return hash == dependencyPathHash ? ANSI_GREEN : ANSI_BLUE;
            };

            if (precise) {
                // Use scanForReferencesDeep to find files containing references
                scanForReferencesDeep(*accessor, CanonPath::root, refPaths, [&](FileRefScanResult result) {
                    auto p2 = result.filePath.isRoot() ? result.filePath.abs() : result.filePath.rel();
                    auto st = accessor->lstat(result.filePath);

                    if (st.type == SourceAccessor::Type::tRegular) {
                        auto contents = accessor->readFile(result.filePath);

                        // For each reference found in this file, extract context
                        for (auto & foundRef : result.foundRefs) {
                            std::string hash(foundRef.hashPart());
                            auto pos = contents.find(hash);
                            if (pos != std::string::npos) {
                                size_t margin = 32;
                                auto pos2 = pos >= margin ? pos - margin : 0;
                                hits[hash].emplace_back(fmt(
                                    "%s: …%s…",
                                    p2,
                                    hilite(
                                        filterPrintable(std::string(contents, pos2, pos - pos2 + hash.size() + margin)),
                                        pos - pos2,
                                        StorePath::HashLen,
                                        getColour(hash))));
                            }
                        }
                    } else if (st.type == SourceAccessor::Type::tSymlink) {
                        auto target = accessor->readLink(result.filePath);

                        // For each reference found in this symlink, show it
                        for (auto & foundRef : result.foundRefs) {
                            std::string hash(foundRef.hashPart());
                            auto pos = target.find(hash);
                            if (pos != std::string::npos)
                                hits[hash].emplace_back(
                                    fmt("%s -> %s", p2, hilite(target, pos, StorePath::HashLen, getColour(hash))));
                        }
                    }
                });
            }

            for (auto & ref : refs) {
                std::string hash(ref.second->path.hashPart());

                bool last = all ? ref == *refs.rbegin() : true;

                for (auto & hit : hits[hash]) {
                    bool first = hit == *hits[hash].begin();
                    logger->cout(
                        "%s%s%s", tailPad, (first ? (last ? treeLast : treeConn) : (last ? treeNull : treeLine)), hit);
                    if (!all)
                        break;
                }

                if (!precise) {
                    logger->cout(
                        "%s%s%s%s" ANSI_NORMAL,
                        firstPad,
                        ref.second->visited ? "\e[38;5;244m" : "",
                        last ? treeLast : treeConn,
                        store->printStorePath(ref.second->path));
                    node.visited = true;
                }

                printNode(*ref.second, tailPad + (last ? treeNull : treeLine), tailPad + (last ? treeNull : treeLine));
            }
        };

        RunPager pager;
        try {
            if (!precise) {
                logger->cout("%s", store->printStorePath(graph.at(packagePath).path));
            }
            printNode(graph.at(packagePath), "", "");
        } catch (BailOut &) {
        }
    }
};

static auto rCmdWhyDepends = registerCommand<CmdWhyDepends>("why-depends");
