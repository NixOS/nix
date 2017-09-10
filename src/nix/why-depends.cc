#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"
#include "progress-bar.hh"
#include "fs-accessor.hh"

using namespace nix;

static std::string hilite(const std::string & s, size_t pos, size_t len)
{
    return
        std::string(s, 0, pos)
        + ANSI_RED
        + std::string(s, pos, len)
        + ANSI_NORMAL
        + std::string(s, pos + len);
}

struct CmdWhyDepends : SourceExprCommand
{
    std::string _package, _dependency;

    CmdWhyDepends()
    {
        expectArg("package", &_package);
        expectArg("dependency", &_dependency);
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
                "To show which files in Hello's closure depend on Glibc:",
                "nix why-depends nixpkgs.hello nixpkgs.glibc"
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

        // FIXME: show the path through the dependency graph.

        bool first = true;

        for (auto & path : closure) {

            if (path == dependencyPath && packagePath != dependencyPath)
                continue;

            if (!store->queryPathInfo(path)->references.count(dependencyPath))
                continue;

            if (!first) std::cerr << "\n";
            first = false;

            std::cerr << fmt("%s:\n", path);

            std::function<void(const Path &)> recurse;

            recurse = [&](const Path & p) {
                auto st = accessor->stat(p);

                auto p2 = p == path ? "/" : std::string(p, path.size() + 1);

                if (st.type == FSAccessor::Type::tDirectory) {
                    auto names = accessor->readDirectory(p);
                    for (auto & name : names)
                        recurse(p + "/" + name);
                }

                else if (st.type == FSAccessor::Type::tRegular) {
                    auto contents = accessor->readFile(p);
                    auto pos = contents.find(dependencyPathHash);
                    if (pos != std::string::npos) {
                        size_t margin = 16;
                        auto pos2 = pos >= margin ? pos - margin : 0;
                        std::string fragment;
                        for (char c : std::string(contents, pos2,
                                pos - pos2 + dependencyPathHash.size() + margin))
                        {
                            fragment += isprint(c) ? c : '.';
                        }

                        std::cerr << fmt("  %s: …%s…\n",
                            p2,
                            hilite(fragment, pos - pos2, storePathHashLen));
                    }
                }

                else if (st.type == FSAccessor::Type::tSymlink) {
                    auto target = accessor->readLink(p);
                    auto pos = target.find(dependencyPathHash);
                    if (pos != std::string::npos) {
                        std::cerr << fmt("  %s -> %s\n", p2, hilite(target, pos, storePathHashLen));
                    }
                }
            };

            recurse(path);
        }
    }
};

static RegisterCommand r1(make_ref<CmdWhyDepends>());
