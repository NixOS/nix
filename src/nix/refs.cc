#include "command.hh"
#include "store-api.hh"
#include "shared.hh"
#include "eval.hh"

using namespace nix;

struct CmdRefs : InstallablesCommand
{
    bool _run = false;
    bool _build = false;
    bool _eval = false;

    CmdRefs()
    {
        mkFlag()
            .longName("run")
            .shortName('r')
            .description("Print dependencies needed to run the package")
            .set(&_run, true);

        mkFlag()
            .longName("build")
            .shortName('b')
            .description("Print dependencies needed to build the package")
            .set(&_build, true);

        mkFlag()
            .longName("eval")
            .shortName('e')
            .description("Print dependencies needed to evaluate the package")
            .set(&_eval, true);
    }

    std::string description() override
    {
        return "list all dependencies of a package";
    }

    Examples examples() override
    {
        return {
            Example{
                "Show all dependencies required to run nixpkgs.hello",
                "nix refs --run nixpkgs.hello"
            },
            Example{
                "Show all dependencies required to build nixpkgs.hello",
                "nix refs --build nixpkgs.hello"
            },
            Example{
                "Show all dependencies required to evaluate, build and run nixpkgs.hello",
                "nix refs --run --build --eval nixpkgs.hello "
            },
        };
    }

    void run(ref<Store> store) override
    {
        if (!_run && !_build && !_eval)
            throw UsageError("Must set at least one of --run, --build, or --eval.");

        StorePathSet dependencies;

        for (auto & i : installables) {
            StorePathSet paths;

            if (_eval) {
                auto state = std::make_shared<EvalState>(searchPath, getStore());

                // force evaluation of package argument
                i->toValue(*state);

                for (auto & d : (*state).importedDrvs)
                    paths.insert(store->parseStorePath(d.first));
            }

            if (_build) {
                for (auto & b : i->toBuildables()) {
                    if (!b.drvPath)
                        throw UsageError("Cannot find build references without a derivation path");
                    paths.insert(b.drvPath->clone());
                }
            }

            if (_run)
                paths.insert(toStorePath(store, Build, i));

            StorePathSet closure;
            store->computeFSClosure(paths, closure, false, true);
            for (auto & c : closure)
                if (!c.isDerivation())
                    dependencies.insert(c.clone());
        }

        auto sorted = store->topoSortPaths(dependencies);
        reverse(sorted.begin(), sorted.end());
        for (auto & i : sorted)
            std::cout << store->printStorePath(i) << std::endl;
    }
};

static auto r1 = registerCommand<CmdRefs>("refs");
