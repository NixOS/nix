#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "json.hh"
#include "value-to-json.hh"

using namespace nix;

struct CmdListOptions : InstallableCommand
{
    CmdListOptions()
    {
    }

    std::string description() override
    {
        return "show the options provided by a Nix configuration";
    }

    //Category category() override { return catSecondary; }

    void run(ref<Store> store) override
    {
        auto state = getEvalState();

        auto vModule = installable->toValue(*state).first;
        state->forceAttrs(*vModule);

        auto aAllOptions = vModule->attrs->get(state->symbols.create("_allOptions"));
        if (!aAllOptions)
            throw Error("module does not have an '_allOptions' attribute");
        state->forceAttrs(*aAllOptions->value);

        auto aFinal = vModule->attrs->get(state->symbols.create("final"));
        if (!aFinal)
            throw Error("module does not have an 'final' attribute");
        state->forceAttrs(*aFinal->value);

        for (const auto & [n, option] : enumerate(aAllOptions->value->attrs->lexicographicOrder())) {
            if (n) logger->stdout("");
            logger->stdout(ANSI_BOLD "%s" ANSI_NORMAL, option->name);

            state->forceAttrs(*option->value);

            std::string description = ANSI_ITALIC "<no description>" ANSI_NORMAL;
            auto aDescription = option->value->attrs->get(state->symbols.create("description"));
            if (aDescription)
                // FIXME: render markdown.
                description = state->forceString(*aDescription->value);
            logger->stdout("  " ANSI_BOLD "Description:" ANSI_NORMAL " %s", description);

            auto aValue = aFinal->value->attrs->get(option->name);
            assert(aValue);

            std::ostringstream str;
            JSONPlaceholder jsonOut(str);
            PathSet context;
            printValueAsJSON(*state, true, *aValue->value, jsonOut, context);
            logger->stdout("  " ANSI_BOLD "Value:" ANSI_NORMAL " %s", str.str());
        }
    }
};

static auto r1 = registerCommand<CmdListOptions>("list-options");
