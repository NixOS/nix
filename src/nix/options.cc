#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"
#include "eval.hh"
#include "eval-inline.hh"
#include "json.hh"
#include "value-to-json.hh"

#include "../cpptoml/cpptoml.h"

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

            std::string doc = ANSI_ITALIC "<no description>" ANSI_NORMAL;
            auto aDoc = option->value->attrs->get(state->symbols.create("doc"));
            if (aDoc)
                // FIXME: render markdown.
                doc = state->forceString(*aDoc->value);
            logger->stdout("  " ANSI_BOLD "Description:" ANSI_NORMAL " %s", doc);

            auto aValue = aFinal->value->attrs->get(option->name);
            assert(aValue);

            try {
                std::ostringstream str;
                JSONPlaceholder jsonOut(str);
                PathSet context;
                printValueAsJSON(*state, true, *aValue->value, jsonOut, context);
                logger->stdout("  " ANSI_BOLD "Value:" ANSI_NORMAL " %s", str.str());
            } catch (EvalError &) {
                // FIXME: should ignore "no default" errors, print
                // other errors.
                logger->stdout("  " ANSI_BOLD "Value:" ANSI_NORMAL " " ANSI_ITALIC "none" ANSI_NORMAL);
            }
        }
    }
};

static auto r1 = registerCommand<CmdListOptions>("list-options");

struct CmdSetOption : InstallableCommand
{
    std::string name;
    std::string value;

    CmdSetOption()
    {
        // FIXME: type?
        expectArg("name", &name);
        expectArg("value", &value);
    }

    std::string description() override
    {
        return "set the value of an option in a Nix module";
    }

    Strings getDefaultFlakeAttrPathPrefixes()
    {
        return {
            "modules.",
        };
    }

    Strings getDefaultFlakeAttrPaths() override
    {
        // FIXME: support this elsewhere.
        return {"modules.default"};
    }

    void run(ref<Store> store) override
    {
        auto state = getEvalState();

        auto installable2 = std::dynamic_pointer_cast<InstallableFlake>(installable);
        if (!installable2)
            throw UsageError("'nix set-option' only works on flakes");

        //auto flake = flake::getFlake(*state, installable2->flakeRef, false);

        auto path = installable2->flakeRef.input.getSourcePath();
        if (!path)
            throw Error("'nix set-option' only works on writable flakes");

        auto tomlPath = *path + "/" + installable2->flakeRef.subdir + "/nix.toml";

        if (!pathExists(tomlPath))
            throw Error("'nix set-option' only works on flakes that have a 'nix.toml' file");

        std::istringstream tomlStream(readFile(tomlPath));
        auto root = cpptoml::parser(tomlStream).parse();

        auto moduleName = *installable2->attrPaths.begin();

        //throw UsageError("'nix set-option' only works on modules, not '%s'", moduleName);
        if (hasPrefix(moduleName, "modules."))
            moduleName = std::string(moduleName, 8);

        printError("PATH = %s %s", tomlPath, moduleName);

        // FIXME: validate module name.
        auto module = root->get_table(moduleName);
        if (!module)
            throw Error("flake '%s' does not have a module named '%s'",
                installable2->flakeRef, moduleName);

        // FIXME: check that option 'name' exists.
        module->insert(name, value);

        std::ostringstream str;
        str << *root;
        writeFile(tomlPath, str.str());

        // FIXME: markChangedFile()?
    }
};

static auto r2 = registerCommand<CmdSetOption>("set-option");
