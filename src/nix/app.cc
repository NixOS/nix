#include "installables.hh"
#include "store-api.hh"
#include "eval-inline.hh"
#include "eval-cache.hh"
#include "names.hh"

namespace nix {

App Installable::toApp(EvalState & state)
{
    auto [cursor, attrPath] = getCursor(state);

    auto type = cursor->getAttr("type")->getString();

    auto checkProgram = [&](const Path & program)
    {
        if (!state.store->isInStore(program))
            throw Error("app program '%s' is not in the Nix store", program);
    };

    if (type == "app") {
        auto [program, context] = cursor->getAttr("program")->getStringWithContext();

        checkProgram(program);

        std::vector<StorePathWithOutputs> context2;
        for (auto & [path, names] : context) {
            if (names.size() != 1)
                throw Error("dynamic drvs not yet supported by this command");
            context2.push_back({state.store->parseStorePath(path), {*names.begin()}});
        }

        return App {
            .context = std::move(context2),
            .program = program,
        };
    }

    else if (type == "derivation") {
        auto drvPath = cursor->forceDerivation();
        auto outPath = cursor->getAttr(state.sOutPath)->getString();
        auto outputName = cursor->getAttr(state.sOutputName)->getString();
        auto name = cursor->getAttr(state.sName)->getString();
        auto aMeta = cursor->maybeGetAttr("meta");
        auto aMainProgram = aMeta ? aMeta->maybeGetAttr("mainProgram") : nullptr;
        auto mainProgram =
            aMainProgram
            ? aMainProgram->getString()
            : DrvName(name).name;
        auto program = outPath + "/bin/" + mainProgram;
        checkProgram(program);
        return App {
            .context = { { drvPath, {outputName} } },
            .program = program,
        };
    }

    else
        throw Error("attribute '%s' has unsupported type '%s'", attrPath, type);
}

}
