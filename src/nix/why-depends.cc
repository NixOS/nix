#include "command.hh"
#include "store-api.hh"
#include "progress-bar.hh"
#include "fs-accessor.hh"
#include "shared.hh"

#include <queue>

using namespace nix;

static std::string hilite(const std::string & s, size_t pos, size_t len,
    const std::string & colour = ANSI_RED)
{
    return
        std::string(s, 0, pos)
        + colour
        + std::string(s, pos, len)
        + ANSI_NORMAL
        + std::string(s, pos + len);
}

static std::string filterPrintable(const std::string & s)
{
    std::string res;
    for (char c : s)
        res += isprint(c) ? c : '.';
    return res;
}

struct CmdWhyDepends : SourceExprCommand
{
    std::string _package, _dependency;
    bool all = false;

    CmdWhyDepends()
    {
        expectArg("package", &_package);
        expectArg("dependency", &_dependency);

        mkFlag()
            .longName("all")
            .shortName('a')
            .description("show all edges in the dependency graph leading from 'package' to 'dependency', rather than just a shortest path")
            .set(&all, true);
    }

    std::string name() override
    {
        return "why-depends";
    }

    std::string description() override
    {
        return "show why a package has another package in its closure";
    }

    Examples examples() override
    {
        return {
            Example{
                "To show one path through the dependency graph leading from Hello to Glibc:",
                "nix why-depends nixpkgs.hello nixpkgs.glibc"
            },
            Example{
                "To show all files and paths in the dependency graph leading from Thunderbird to libX11:",
                "nix why-depends --all nixpkgs.thunderbird nixpkgs.xorg.libX11"
            },
            Example{
                "To show why Glibc depends on itself:",
                "nix why-depends nixpkgs.glibc nixpkgs.glibc"
            },
        };
    }

    void run(ref<Store> store) override
    {
        auto package = parseInstallable(*this, store, _package, false);
        auto packagePath = toStorePath(store, Build, package);
        auto dependency = parseInstallable(*this, store, _dependency, false);
        auto dependencyPath = toStorePath(store, NoBuild, dependency);
        auto dependencyPathHash = storePathToHash(dependencyPath);

        PathSet closure;
        store->computeFSClosure({packagePath}, closure, false, false);

        if (!closure.count(dependencyPath)) {
            printError("'%s' does not depend on '%s'", package->what(), dependency->what());
            return;
        }

        stopProgressBar(); // FIXME

        auto accessor = store->getFSAccessor();

        auto const inf = std::numeric_limits<size_t>::max();

        struct Node
        {
            Path path;
            PathSet refs;
            PathSet rrefs;
            size_t dist = inf;
            Node * prev = nullptr;
            bool queued = false;
            bool visited = false;
        };

        std::map<Path, Node> graph;

        for (auto & path : closure)
            graph.emplace(path, Node{path, store->queryPathInfo(path)->references});

        // Transpose the graph.
        for (auto & node : graph)
            for (auto & ref : node.second.refs)
                graph[ref].rrefs.insert(node.first);

        /* Run Dijkstra's shortest path algorithm to get the distance
           of every path in the closure to 'dependency'. */
        graph[dependencyPath].dist = 0;

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
        std::function<void(Node &, const string &, const string &)> printNode;

        const string treeConn = "╠═══";
        const string treeLast = "╚═══";
        const string treeLine = "║   ";
        const string treeNull = "    ";

        struct BailOut { };

        printNode = [&](Node & node, const string & firstPad, const string & tailPad) {
            assert(node.dist != inf);
            std::cout << fmt("%s%s%s%s" ANSI_NORMAL "\n",
                firstPad,
                node.visited ? "\e[38;5;244m" : "",
                firstPad != "" ? "=> " : "",
                node.path);

            if (node.path == dependencyPath && !all
                && packagePath != dependencyPath)
                throw BailOut();

            if (node.visited) return;
            node.visited = true;

            /* Sort the references by distance to `dependency` to
               ensure that the shortest path is printed first. */
            std::multimap<size_t, Node *> refs;
            std::set<std::string> hashes;

            for (auto & ref : node.refs) {
                if (ref == node.path && packagePath != dependencyPath) continue;
                auto & node2 = graph.at(ref);
                if (node2.dist == inf) continue;
                refs.emplace(node2.dist, &node2);
                hashes.insert(storePathToHash(node2.path));
            }

            /* For each reference, find the files and symlinks that
               contain the reference. */
            std::map<std::string, Strings> hits;

            std::function<void(const Path &)> visitPath;

            visitPath = [&](const Path & p) {
                auto st = accessor->stat(p);

                auto p2 = p == node.path ? "/" : std::string(p, node.path.size() + 1);

                auto getColour = [&](const std::string & hash) {
                    return hash == dependencyPathHash ? ANSI_GREEN : ANSI_BLUE;
                };

                if (st.type == FSAccessor::Type::tDirectory) {
                    auto names = accessor->readDirectory(p);
                    for (auto & name : names)
                        visitPath(p + "/" + name);
                }

                else if (st.type == FSAccessor::Type::tRegular) {
                    auto contents = accessor->readFile(p);

                    for (auto & hash : hashes) {
                        auto pos = contents.find(hash);
                        if (pos != std::string::npos) {
                            size_t margin = 32;
                            auto pos2 = pos >= margin ? pos - margin : 0;
                            hits[hash].emplace_back(fmt("%s: …%s…\n",
                                    p2,
                                    hilite(filterPrintable(
                                            std::string(contents, pos2, pos - pos2 + hash.size() + margin)),
                                        pos - pos2, storePathHashLen,
                                        getColour(hash))));
                        }
                    }
                }

                else if (st.type == FSAccessor::Type::tSymlink) {
                    auto target = accessor->readLink(p);

                    for (auto & hash : hashes) {
                        auto pos = target.find(hash);
                        if (pos != std::string::npos)
                            hits[hash].emplace_back(fmt("%s -> %s\n", p2,
                                    hilite(target, pos, storePathHashLen, getColour(hash))));
                    }
                }
            };

            // FIXME: should use scanForReferences().

            visitPath(node.path);

            RunPager pager;
            for (auto & ref : refs) {
                auto hash = storePathToHash(ref.second->path);

                bool last = all ? ref == *refs.rbegin() : true;

                for (auto & hit : hits[hash]) {
                    bool first = hit == *hits[hash].begin();
                    std::cout << tailPad
                              << (first ? (last ? treeLast : treeConn) : (last ? treeNull : treeLine))
                              << hit;
                    if (!all) break;
                }

                printNode(*ref.second,
                    tailPad + (last ? treeNull : treeLine),
                    tailPad + (last ? treeNull : treeLine));
            }
        };

        try {
            printNode(graph.at(packagePath), "", "");
        } catch (BailOut & ) { }
    }
};

static RegisterCommand r1(make_ref<CmdWhyDepends>());
